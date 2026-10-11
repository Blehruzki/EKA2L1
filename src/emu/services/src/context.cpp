/*
 * Copyright (c) 2018 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
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

#include <kernel/kernel.h>
#include <kernel/server.h>
#include <mem/ptr.h>
#include <system/epoc.h>

#include <services/context.h>
#include <utils/des.h>
#include <utils/err.h>
#include <utils/sec.h>

#include <config/config.h>

#include <mutex>
#include <map>
#include <set>
#include <string>
#include <tuple>

namespace eka2l1 {
    namespace service {
        ipc_context::ipc_context() {
        }

        ipc_context::~ipc_context() {
            if (auto_deref)
                msg->unref();
        }

        template <typename T>
        std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>, std::optional<T>>
        get_integral_arg_from_msg(ipc_msg_ptr &msg, const int idx) {
            if (idx >= 4) {
                return std::nullopt;
            }

            return *reinterpret_cast<T *>(&msg->args.args[idx]);
        }

        template <>
        std::optional<std::uint8_t> ipc_context::get_argument_value(const int idx) {
            return get_integral_arg_from_msg<std::uint8_t>(msg, idx);
        }

        template <>
        std::optional<std::uint16_t> ipc_context::get_argument_value(const int idx) {
            return get_integral_arg_from_msg<std::uint16_t>(msg, idx);
        }

        template <>
        std::optional<std::uint32_t> ipc_context::get_argument_value(const int idx) {
            return get_integral_arg_from_msg<std::uint32_t>(msg, idx);
        }

        template <>
        std::optional<std::int8_t> ipc_context::get_argument_value(const int idx) {
            return get_integral_arg_from_msg<std::int8_t>(msg, idx);
        }

        template <>
        std::optional<std::int16_t> ipc_context::get_argument_value(const int idx) {
            return get_integral_arg_from_msg<std::int16_t>(msg, idx);
        }

        template <>
        std::optional<std::int32_t> ipc_context::get_argument_value(const int idx) {
            return get_integral_arg_from_msg<std::int32_t>(msg, idx);
        }

        template <>
        std::optional<float> ipc_context::get_argument_value(const int idx) {
            return get_integral_arg_from_msg<float>(msg, idx);
        }

        template <>
        std::optional<double> ipc_context::get_argument_value(const int idx) {
            return get_integral_arg_from_msg<double>(msg, idx);
        }

        template <>
        std::optional<std::u16string> ipc_context::get_argument_value(const int idx) {
            if (idx >= 4) {
                return std::nullopt;
            }

            const ipc_arg_type iatype = msg->args.get_arg_type(idx);
            const bool is_descriptor = (int)iatype & (int)ipc_arg_type::flag_des;
            const bool is_16_bit = (int)iatype & (int)ipc_arg_type::flag_16b;

            if (sys->get_kernel_system()->is_eka1() || (is_descriptor && is_16_bit)) {
                kernel::process *own_pr = msg->own_thr->owning_process();
                eka2l1::epoc::desc16 *des = ptr<epoc::desc16>(msg->args.args[idx]).get(own_pr);

                if (!des) {
                    return std::nullopt;
                }

                return des->to_std_string(own_pr);
            }

            return std::nullopt;
        }

        template <>
        std::optional<std::string> ipc_context::get_argument_value(const int idx) {
            if (idx >= 4) {
                return std::nullopt;
            }

            const ipc_arg_type iatype = msg->args.get_arg_type(idx);
            const bool is_descriptor = (int)iatype & (int)ipc_arg_type::flag_des;
            const bool is_16_bit = (int)iatype & (int)ipc_arg_type::flag_16b;

            // If it has descriptor flag and it doesn't have an 16-bit flag, it should be 8-bit one.
            if (sys->get_kernel_system()->is_eka1() || (is_descriptor && !is_16_bit)) {
                kernel::process *own_process = msg->own_thr->owning_process();
                eka2l1::epoc::desc8 *des = ptr<epoc::desc8>(msg->args.args[idx]).get(own_process);

                if (!des) {
                    return std::nullopt;
                }

                return des->to_std_string(msg->own_thr->owning_process());
            }

            return std::nullopt;
        }

        void ipc_context::complete(int res) {
            if (msg->request_sts) {
                kernel_system *kern = sys->get_kernel_system();
                (msg->request_sts.get(msg->own_thr->owning_process()))->set(res, kern->is_eka1());

                // Avoid signal twice to cause undefined behavior
                if (!signaled) {
                    msg->own_thr->signal_request();
                    signaled = true;
                }
            }
        }

        void ipc_context::complete_unhandled(const char *server_name, const bool prefer_success) {
            const int opcode = msg ? msg->function : -1;
            const char *name = server_name ? server_name : "<unknown server>";

            // Log once per (server, opcode). Unhandled opcodes are typically hit from inner
            // dispatch loops, so logging every occurrence buries the rest of the trace.
            {
                static std::mutex seen_lock;
                static std::set<std::pair<std::string, int>> seen;

                const std::lock_guard<std::mutex> guard(seen_lock);

                if (seen.emplace(name, opcode).second) {
                    // Deliberately ERROR, not WARN: this is the primary compatibility signal
                    // for "this app asked for something we do not implement", and the sites
                    // it replaced all logged at ERROR. Anything quieter gets filtered away by
                    // common log-filter settings (e.g. "Service.Track:error") and the signal
                    // is lost exactly when it is most needed.
                    LOG_ERROR(SERVICE_TRACK, "Unimplemented opcode 0x{:X} for {}", opcode, name);
                }
            }

            const std::string &policy = sys->get_config()->unhandled_opcode_policy;

            if (policy == "hang") {
                // Deliberately leave the message outstanding, reproducing the old behaviour.
                return;
            }

            // Loop detection. Completing an unrecognised opcode is right for a synchronous request,
            // but an asynchronous notification request stays outstanding on a real device until the
            // event happens; completing it makes the client re-subscribe at once and spin (measured:
            // 263K sends in 10s of !EtelServer 24011 from Clock). We do not know which opcodes are
            // notifiers, so instead count repeats per (server, opcode, calling thread) and, once an
            // opcode keeps coming back, stop completing it. The message is left outstanding exactly
            // like the "hang" policy above; the kernel completes it with error_session_closed when
            // the session goes away (session::detatch), so nothing leaks.
            {
                // Hits at which an opcode starts being left pending: the first (K - 1) are completed.
                constexpr std::uint32_t LOOP_PARK_THRESHOLD = 8;

                // Upper bound on tracked keys. Hitting it only forgets old counts (each key then gets
                // a fresh budget), which is harmless, whereas unbounded growth is not.
                constexpr std::size_t MAX_TRACKED_KEYS = 4096;

                // Keyed by thread unique id rather than pointer, so a recycled thread object does not
                // inherit an old budget.
                using hit_key = std::tuple<std::string, int, kernel::uid>;

                static std::mutex hits_lock;
                static std::map<hit_key, std::uint32_t> hits;

                const kernel::uid thread_id = (msg && msg->own_thr) ? msg->own_thr->unique_id() : 0;
                bool park = false;
                bool first_park = false;

                {
                    const std::lock_guard<std::mutex> guard(hits_lock);

                    if (hits.size() >= MAX_TRACKED_KEYS) {
                        hits.clear();
                    }

                    std::uint32_t &count = hits[hit_key(name, opcode, thread_id)];

                    if (count < LOOP_PARK_THRESHOLD) {
                        // Saturates at the threshold, so it cannot overflow however long the loop runs.
                        count++;
                        first_park = (count == LOOP_PARK_THRESHOLD);
                    }

                    park = (count >= LOOP_PARK_THRESHOLD);
                }

                if (first_park) {
                    // ERROR on purpose (see above): this tells us an app is stuck on a notifier.
                    LOG_ERROR(SERVICE_TRACK, "Opcode 0x{:X} for {} hit {} times by thread {} (id {}), leaving it pending "
                        "(likely an unimplemented notification request)", opcode, name, LOOP_PARK_THRESHOLD,
                        (msg && msg->own_thr) ? msg->own_thr->name() : std::string("<unknown>"), thread_id);
                }

                if (park) {
                    return;
                }
            }

            if (prefer_success || (policy == "success")) {
                complete(epoc::error_none);
                return;
            }

            complete(epoc::error_not_supported);
        }

        int ipc_context::flag() const {
            return msg->args.flag;
        }

        bool ipc_context::write_arg(int idx, uint32_t data) {
            if (idx >= 0 && idx < 4 && (sys->get_kernel_system()->is_eka1() || (msg->args.get_arg_type(idx) == ipc_arg_type::handle))) {
                msg->args.args[idx] = data;
                return true;
            }

            return false;
        }

        bool ipc_context::write_arg(int idx, const std::u16string &data) {
            if (idx >= 4) {
                return false;
            }

            ipc_arg_type arg_type = msg->args.get_arg_type(idx);

            if (sys->get_kernel_system()->is_eka1() || ((int)arg_type & ((int)ipc_arg_type::flag_des | (int)ipc_arg_type::flag_16b))) {
                eka2l1::epoc::desc16 *des = ptr<epoc::desc16>(msg->args.args[idx]).get(msg->own_thr->owning_process());

                des->assign(msg->own_thr->owning_process(), data);

                return true;
            }

            return false;
        }

        bool ipc_context::write_data_to_descriptor_argument(int idx, const uint8_t *data, uint32_t len, int *err_code, const bool auto_shrink_to_fit) {
            if (idx >= 4 || idx < 0) {
                return false;
            }

            ipc_arg_type arg_type = msg->args.get_arg_type(idx);
            const bool is_eka1 = sys->get_kernel_system()->is_eka1();

            if (is_eka1 || ((int)arg_type & (int)ipc_arg_type::flag_des)) {
                kernel::process *own_pr = msg->own_thr->owning_process();

                // Please don't change the order
                // On EKA1 force write argument package to be 8-bit descriptor only
                if (!is_eka1 && ((int)arg_type & (int)ipc_arg_type::flag_16b)) {
                    eka2l1::epoc::desc16 *des = ptr<epoc::desc16>(msg->args.args[idx]).get(own_pr);

                    // We can't handle odd length
                    assert(len % 2 == 0);

                    std::uint32_t write_size = len / 2;
                    const std::uint32_t des_to_write_size = des->get_max_length(own_pr);

                    if (auto_shrink_to_fit) {
                        write_size = common::min(write_size, des_to_write_size);
                    } else if (des_to_write_size < write_size) {
                        err_code ? (*err_code = -2) : 0;
                        return false;
                    }

                    des->assign(own_pr, data, write_size * 2);
                } else {
                    eka2l1::epoc::des8 *des = ptr<epoc::des8>(msg->args.args[idx]).get(own_pr);

                    std::uint32_t write_size = len;
                    const std::uint32_t des_to_write_size = des->get_max_length(own_pr);

                    if (auto_shrink_to_fit) {
                        write_size = common::min(write_size, des_to_write_size);
                    } else if (des_to_write_size < write_size) {
                        err_code ? (*err_code = -2) : 0;
                        return false;
                    }

                    des->assign(own_pr, data, write_size);
                }

                return true;
            }

            (err_code) ? (*err_code = -1) : 0;
            return false;
        }

        std::uint8_t *ipc_context::get_descriptor_argument_ptr(int idx) {
            const ipc_arg_type arg_type = msg->args.get_arg_type(idx);

            if (sys->get_kernel_system()->is_eka1() || ((int)arg_type & (int)ipc_arg_type::flag_des)) {
                kernel::process *own_pr = msg->own_thr->owning_process();
                eka2l1::epoc::des8 *des = ptr<epoc::des8>(msg->args.args[idx]).get(own_pr);

                if (!des) {
                    return nullptr;
                }

                return reinterpret_cast<std::uint8_t *>(des->get_pointer_raw(own_pr));
            }

            return nullptr;
        }

        std::size_t ipc_context::get_argument_max_data_size(int idx) {
            if (idx >= 4 || idx < 0) {
                return static_cast<std::size_t>(-1);
            }

            const ipc_arg_type arg_type = msg->args.get_arg_type(idx);
            const bool is_eka1 = sys->get_kernel_system()->is_eka1();

            if (!is_eka1) {
                if ((static_cast<int>(arg_type) & static_cast<int>(ipc_arg_type::unspecified))
                    || (static_cast<int>(arg_type) & static_cast<int>(ipc_arg_type::handle))) {
                    return 4;
                }
            }

            // Cast it to descriptor
            kernel::process *own_pr = msg->own_thr->owning_process();
            epoc::des8 *descriptor = ptr<epoc::des8>(msg->args.args[idx]).get(own_pr);

            if (!is_eka1 && (static_cast<int>(arg_type) & static_cast<int>(ipc_arg_type::flag_16b))) {
                return descriptor->get_max_length(own_pr) * 2;
            }

            return descriptor->get_max_length(own_pr);
        }

        std::size_t ipc_context::get_argument_data_size(int idx) {
            if (idx >= 4 || idx < 0) {
                return static_cast<std::size_t>(-1);
            }

            const ipc_arg_type arg_type = msg->args.get_arg_type(idx);
            const bool is_eka1 = sys->get_kernel_system()->is_eka1();

            if (!is_eka1) {
                if ((static_cast<int>(arg_type) & static_cast<int>(ipc_arg_type::unspecified))
                    || (static_cast<int>(arg_type) & static_cast<int>(ipc_arg_type::handle))) {
                    return 4;
                }
            }

            // Cast it to descriptor
            kernel::process *own_pr = msg->own_thr->owning_process();
            epoc::des8 *descriptor = ptr<epoc::des8>(msg->args.args[idx]).get(own_pr);

            if (!is_eka1 && (static_cast<int>(arg_type) & static_cast<int>(ipc_arg_type::flag_16b))) {
                return descriptor->get_length() * 2;
            }

            return descriptor->get_length();
        }

        bool ipc_context::set_descriptor_argument_length(const int idx, const std::uint32_t len) {
            ipc_arg_type arg_type = msg->args.get_arg_type(idx);

            if (sys->get_kernel_system()->is_eka1() || ((int)arg_type & (int)ipc_arg_type::flag_des)) {
                kernel::process *own_pr = msg->own_thr->owning_process();
                eka2l1::epoc::des8 *des = ptr<epoc::des8>(msg->args.args[idx]).get(own_pr);

                des->set_length(own_pr, len);
                return true;
            }

            return false;
        }

        bool ipc_context::satisfy(epoc::security_policy &policy, epoc::security_info *missing) {
            return msg->own_thr->owning_process()->satisfy(policy, missing);
        }

        void server::connect(service::ipc_context &ctx) {
            ctx.complete(0);
        }

        void server::disconnect(service::ipc_context &ctx) {
            ctx.complete(0);
        }

        // Processed asynchronously, use for HLE service where accepted function
        // is fetched imm
        void server::process_accepted_msg() {
            ipc_msg_ptr process_msg = nullptr;
            receive(process_msg);

            if (!process_msg) {
                return;
            }

            int func = process_msg->function;

            auto func_ite = ipc_funcs.find(func);
            config::state *conf = sys->get_config();

            if (func_ite == ipc_funcs.end()) {
                if (unhandle_callback_enable) {
                    ipc_context context;

                    context.sys = sys;
                    context.msg = process_msg;

                    on_unhandled_opcode(context);

                    return;
                }

                // An opcode with no entry in the dispatch table still has to be answered.
                // Logging and returning leaves the message outstanding and the calling
                // guest thread waiting forever: no panic, no error, no exit, it simply
                // stops. That is how !CentralRepository opcode 4 stalled PlayServer while
                // it held the global playserver.exe mutex, which in turn blocked every
                // other N-Gage process behind it.
                //
                // The servers that dispatch through a switch already route their default
                // case here; this is the same gap on the REGISTER_IPC path, which was
                // missed when those were swept. complete_unhandled carries the loop-aware
                // parking, so an unimplemented notification request still will not spin.
                ipc_context context;

                context.sys = sys;
                context.msg = process_msg;

                context.complete_unhandled(obj_name.c_str());
                return;
            }

            ipc_func ipf = func_ite->second;
            ipc_context context;
            context.sys = sys;
            context.msg = process_msg;

            if (conf->log_ipc) {
                LOG_INFO(SERVICE_TRACK, "Calling IPC: {}, id: {}", ipf.name, func);
            }

            ipf.wrapper(context);
        }
    }
}
