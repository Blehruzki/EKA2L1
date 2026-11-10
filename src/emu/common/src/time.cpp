/*
 * Copyright (c) 2019 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
 * 
 * Initial contributor: pent0
 * Contributors:
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <chrono>
#include <thread>
#include <common/algorithm.h>
#include <common/platform.h>
#include <common/pchook.h>
#include <common/time.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <string>

#if EKA2L1_PLATFORM(WIN32)
#include <Windows.h>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")
#elif EKA2L1_PLATFORM(DARWIN)
#include <CoreFoundation/CoreFoundation.h>
#elif EKA2L1_PLATFORM(POSIX)
#include <limits.h>
#include <unistd.h>
#endif

namespace eka2l1::common {
    pc_hook g_pc_hook = { { 0, 0, 0, 0, 0, 0, 0, 0 }, nullptr };
    std::atomic<std::uint64_t> g_cpu_cycle_counter{0};
    std::atomic<std::uint64_t> g_cpu_exec_ns{0};

    // EKA2L1_TIMESCALE=<D>: every guest clock (the kernel timer's base: UniversalTime,
    // HomeTime, the tick calls, every timer) runs D times slower than the host, and the
    // CPU cap and EKA2L1_GPUMODEL's charges are paid in host time scaled by D -- a bench
    // in slow motion whose proportions are the modelled phone's, for a host too slow to
    // emulate it in real time. 1 (off) by default.
    double timescale() {
        static const double ts = []() -> double {
            const char *w = std::getenv("EKA2L1_TIMESCALE");
            const double v = w ? std::strtod(w, nullptr) : 1.0;
            return v >= 1.0 ? v : 1.0;
        }();
        return ts;
    }

    static std::uint64_t cpu_cap_ips() {
        static const std::uint64_t ips = []() -> std::uint64_t {
            const char *w = std::getenv("EKA2L1_CPUCAP");
            return w ? std::strtoull(w, nullptr, 10) : 0;
        }();
        return ips;
    }

    // Owed host sleep, carried between the cap's calls (both run on the CPU thread).
    static std::int64_t g_cap_debt_ns = 0;
    // Host time inside the current slice already accounted for by cpu_cap_pay_service.
    static std::int64_t g_cap_excluded_ns = 0;
    static constexpr std::int64_t kSleepNs = 1000000;

    // in_slice: called from inside the core's slice, whose host time the core adds to
    // g_cpu_exec_ns itself
    // Whether the host keeps up with the model, for calibrating a bench: a line every ~5 s
    // when EKA2L1_CPUCAP_STATS=1 -- the share of host time slept, and the guest time lost
    // where the host was slower than the model (a bench that loses much is host-bound:
    // raise EKA2L1_TIMESCALE).
    static std::int64_t g_cap_slept_ns = 0, g_cap_lost_ns = 0;

    static void cpu_cap_stats() {
        static const bool on = []() { const char *w = std::getenv("EKA2L1_CPUCAP_STATS"); return w && *w == '1'; }();
        if (!on)
            return;
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        const std::int64_t span = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count();
        if (span < 5000000000LL)
            return;
        std::fprintf(stderr, "EKA2L1_CPUCAP: slept %.0f%% of host time; host behind the model by %.1f ms of host time (%.1f%%)\n",
            100.0 * g_cap_slept_ns / span, g_cap_lost_ns / 1e6, 100.0 * g_cap_lost_ns / span);
        g_cap_slept_ns = 0;
        g_cap_lost_ns = 0;
        last = now;
    }

    static void cpu_cap_pay_debt(const bool in_slice) {
        cpu_cap_stats();
        if (g_cap_debt_ns > kSleepNs) {
            const auto before = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::nanoseconds(g_cap_debt_ns));
            const std::int64_t slept = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - before).count();
            if (!in_slice)
                g_cpu_exec_ns.fetch_add(slept, std::memory_order_relaxed);
            g_cap_debt_ns -= slept;
            g_cap_excluded_ns += slept;
            g_cap_slept_ns += slept;
        } else if (g_cap_debt_ns < -kSleepNs) {
            // The host was slower than the cap: no credit carried forward.
            g_cap_lost_ns -= g_cap_debt_ns;
            g_cap_debt_ns = 0;
        }
    }

    void cpu_cap_pay_service(const std::uint64_t guest_ns, const std::uint64_t host_ns) {
        if (!cpu_cap_ips())
            return;
        const std::int64_t owed = static_cast<std::int64_t>(static_cast<double>(guest_ns) * timescale()) - static_cast<std::int64_t>(host_ns);
        // a host slower than the model at this call banks no credit for the guest's code
        if (owed > 0)
            g_cap_debt_ns += owed;
        else
            g_cap_lost_ns -= owed;
        g_cap_excluded_ns += static_cast<std::int64_t>(host_ns);
        cpu_cap_pay_debt(true);
    }

    std::uint64_t cycle_tick_ips() {
        static const std::uint64_t ips = []() -> std::uint64_t {
            const char *w = std::getenv("EKA2L1_CYCLETICK");
            return w ? std::strtoull(w, nullptr, 10) : 0;
        }();
        return ips;
    }

    void cpu_cap_throttle(const std::uint64_t instructions, const std::uint64_t host_ns) {
        const std::uint64_t ips = cpu_cap_ips();
        g_cpu_exec_ns.fetch_add(host_ns, std::memory_order_relaxed);
        if (!ips) {
            return;
        }

        // Debt: how much longer the modelled CPU would have taken to execute what
        // the host just executed, in nanoseconds. Only time inside the CPU core
        // counts, so idle waits and host-side services neither bank credit nor
        // eat into it. Paid off in sleeps of at least kSleepNs: a sleep costs tens
        // of microseconds of overshoot, so a sleep per short time slice would hold
        // the guest below the cap. The measured length of each sleep is what is
        // subtracted, which absorbs that overshoot too. The modelled guest time is
        // paid in host time scaled by EKA2L1_TIMESCALE; host time a modelled service
        // call already settled (cpu_cap_pay_service) is not the instructions'.
        const std::int64_t own_ns = static_cast<std::int64_t>(host_ns) - g_cap_excluded_ns;
        g_cap_excluded_ns = 0;
        g_cap_debt_ns += static_cast<std::int64_t>(static_cast<double>((instructions * 1000) / ips) * timescale()) - (own_ns > 0 ? own_ns : 0);
        cpu_cap_pay_debt(false);
        g_cap_excluded_ns = 0;
    }

    std::uint64_t get_current_utc_time_in_microseconds_since_epoch() {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::uint64_t get_current_utc_time_in_microseconds_since_0ad() {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
            + ad_epoc_dist_microsecs;
    }

    std::uint64_t get_current_utc_time_in_nanoseconds_since_epoch() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::uint64_t convert_microsecs_epoch_to_0ad(const std::uint64_t micsecs) {
        return micsecs * microsecs_per_sec + ad_epoc_dist_microsecs;
    }

    std::uint64_t convert_microsecs_win32_1601_epoch_to_0ad(const std::uint64_t micsecs) {
        return micsecs / 10 + ad_win32_epoch_dist_microsecs;
    }

    int get_current_utc_offset() {
        const std::time_t current_time = std::time(nullptr);
        return get_local_time_zone_info(static_cast<std::int64_t>(current_time)).offset_seconds;
    }

    local_time_zone_info get_local_time_zone_info(const std::int64_t unix_seconds) {
        const std::time_t timestamp = static_cast<std::time_t>(unix_seconds);
        std::tm local_time{};

#if EKA2L1_PLATFORM(WIN32)
        if (localtime_s(&local_time, &timestamp) != 0) {
            return { 0, false };
        }

        std::tm local_copy = local_time;
        const std::time_t local_as_utc = _mkgmtime(&local_copy);
        return {
            static_cast<int>(local_as_utc - timestamp),
            local_time.tm_isdst > 0
        };
#else
        if (!localtime_r(&timestamp, &local_time)) {
            return { 0, false };
        }

        return {
            static_cast<int>(local_time.tm_gmtoff),
            local_time.tm_isdst > 0
        };
#endif
    }

    std::string get_current_time_zone_name() {
        const char *environment_name = std::getenv("TZ");
        if (environment_name && environment_name[0] != '\0' && environment_name[0] != ':') {
            return environment_name;
        }

#if EKA2L1_PLATFORM(WIN32)
        DYNAMIC_TIME_ZONE_INFORMATION info{};
        if (GetDynamicTimeZoneInformation(&info) != TIME_ZONE_ID_INVALID) {
            const int needed = WideCharToMultiByte(CP_UTF8, 0, info.TimeZoneKeyName, -1,
                nullptr, 0, nullptr, nullptr);
            if (needed > 1) {
                std::string result(static_cast<std::size_t>(needed), '\0');
                WideCharToMultiByte(CP_UTF8, 0, info.TimeZoneKeyName, -1,
                    result.data(), needed, nullptr, nullptr);
                result.resize(static_cast<std::size_t>(needed - 1));
                return result;
            }
        }
#elif EKA2L1_PLATFORM(DARWIN)
        CFTimeZoneRef zone = CFTimeZoneCopySystem();
        if (zone) {
            CFStringRef name = CFTimeZoneGetName(zone);
            const CFIndex length = name ? CFStringGetLength(name) : 0;
            const CFIndex maximum = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
            std::string result;

            if (maximum > 1) {
                result.resize(static_cast<std::size_t>(maximum));
                if (CFStringGetCString(name, result.data(), maximum, kCFStringEncodingUTF8)) {
                    result.resize(std::char_traits<char>::length(result.c_str()));
                } else {
                    result.clear();
                }
            }

            CFRelease(zone);
            if (!result.empty()) {
                return result;
            }
        }
#elif EKA2L1_PLATFORM(POSIX)
        char link_target[PATH_MAX + 1]{};
        const ssize_t length = readlink("/etc/localtime", link_target, PATH_MAX);
        if (length > 0) {
            link_target[length] = '\0';
            const std::string path(link_target);
            const std::string marker = "/zoneinfo/";
            const std::size_t marker_position = path.find(marker);
            if (marker_position != std::string::npos) {
                return path.substr(marker_position + marker.size());
            }
        }
#endif

        const std::time_t current_time = std::time(nullptr);
        std::tm local_time{};
#if EKA2L1_PLATFORM(WIN32)
        if (localtime_s(&local_time, &current_time) == 0 && _tzname[0]) {
            return _tzname[local_time.tm_isdst > 0 ? 1 : 0];
        }
#else
        if (localtime_r(&current_time, &local_time) && local_time.tm_zone) {
            return local_time.tm_zone;
        }
#endif
        return "UTC";
    }

    struct basic_teletimer_micro : public teletimer {
        std::uint64_t start_;
        std::uint64_t end_;

        std::uint32_t target_freq_;

    public:
        explicit basic_teletimer_micro(const std::uint32_t freq)
            : target_freq_(freq) {
        }

        ~basic_teletimer_micro() override {
        }

        void start() override {
            start_ = get_current_utc_time_in_microseconds_since_epoch();
            end_ = 0;
        }

        void stop() override {
            end_ = get_current_utc_time_in_microseconds_since_epoch();
        }

        bool set_target_frequency(const std::uint32_t freq) override {
            target_freq_ = freq;
            return true;
        }

        std::uint64_t ticks() override {
            return multiply_and_divide_qwords(microseconds(), target_freq_, 1000000);
        }

        std::uint64_t microseconds() override {
            const std::uint64_t host = (end_ == 0) ? get_current_utc_time_in_microseconds_since_epoch() - start_ : end_ - start_;
            const double ts = timescale();
            return ts > 1.0 ? static_cast<std::uint64_t>(static_cast<double>(host) / ts) : host;
        }

        std::uint64_t nanoseconds() override {
            return microseconds() * 1000;
        }
    };

    // EKA2L1_DETTICK: a deterministic virtual clock in place of the wall clock.
    // microseconds() advances by a fixed step on each read, so the value the
    // guest sees at a given point in its own instruction stream is the same on
    // every launch. A diagnostic for guest code whose control flow depends on
    // User::TickCount (which EKA2L1 otherwise serves from host wall time, and
    // so varies run to run with host load). The step defaults to 50 us and is
    // overridable with EKA2L1_DETTICK=<microseconds-per-read>.
    struct deterministic_teletimer : public teletimer {
        std::uint64_t now_ = 0;
        std::uint64_t step_;
        std::uint32_t target_freq_;
        explicit deterministic_teletimer(const std::uint32_t freq, std::uint64_t step)
            : step_(step), target_freq_(freq) {}
        void start() override { now_ = 0; }
        void stop() override {}
        bool set_target_frequency(const std::uint32_t freq) override { target_freq_ = freq; return true; }
        std::uint64_t ticks() override { return multiply_and_divide_qwords(microseconds(), target_freq_, 1000000); }
        std::uint64_t microseconds() override { now_ += step_; return now_; }
        std::uint64_t nanoseconds() override { return microseconds() * 1000; }
    };

    std::unique_ptr<teletimer> make_teletimer(const std::uint32_t target_frequency) {
        const char *det = std::getenv("EKA2L1_DETTICK");
        if (det) {
            std::uint64_t step = std::strtoull(det, nullptr, 10);
            if (step == 0) step = 50;
            return std::make_unique<deterministic_teletimer>(target_frequency, step);
        }
        return std::make_unique<basic_teletimer_micro>(target_frequency);
    }

#if EKA2L1_PLATFORM(WIN32)
    static constexpr DWORD MILLISECS_SOLUTION_PERIOD_HR = 1;
#endif

    high_resolution_timer_period_guard::high_resolution_timer_period_guard()
        : set_(false) {
    }

    high_resolution_timer_period_guard::~high_resolution_timer_period_guard() {
#if EKA2L1_PLATFORM(WIN32)
        if (set_) {
            timeEndPeriod(MILLISECS_SOLUTION_PERIOD_HR);
        }
#endif
    }

    void high_resolution_timer_period_guard::toogle() {
#if EKA2L1_PLATFORM(WIN32)
        if (set_) {
            timeEndPeriod(MILLISECS_SOLUTION_PERIOD_HR);
        } else {
            timeBeginPeriod(MILLISECS_SOLUTION_PERIOD_HR);
        }
#endif

        set_ = !set_;
    }
}
