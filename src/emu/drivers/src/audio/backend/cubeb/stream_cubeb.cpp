/*
 * Copyright (c) 2020 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project.
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

#include <drivers/audio/backend/cubeb/stream_cubeb.h>
#include <drivers/audio/audio.h>

#include <common/algorithm.h>
#include <common/log.h>
#include <common/platform.h>

#include <chrono>
#include <cstdlib>
#include <vector>

namespace eka2l1::drivers {
    static long data_callback_redirector(cubeb_stream *stm, void *user,
        const void *input_buffer, void *output_buffer, long nframes) {
        cubeb_audio_stream_base *stream = reinterpret_cast<cubeb_audio_stream_base *>(user);
        return static_cast<long>(stream->call_callback(input_buffer ? 
            reinterpret_cast<std::int16_t *>(const_cast<void*>(input_buffer))
            : reinterpret_cast<std::int16_t *>(output_buffer), nframes));
    }

    void state_callback_redirector(cubeb_stream *stream, void *user_data, cubeb_state state) {
    }

    cubeb_audio_stream_base::cubeb_audio_stream_base(cubeb *context, const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback, bool is_recording) 
        : stream_(nullptr)
        , callback_(callback)
        , idled_frames_(0)
        , internal_channels_(channels)
        , in_action_(false)
        , software_fallback_(false)
        , fallback_sample_rate_(sample_rate)
        , fallback_running_(false)
        , fallback_frames_(0) {
        cubeb_stream_params params;
        params.format = CUBEB_SAMPLE_S16LE;
        params.rate = sample_rate;
        params.channels = channels;
        params.layout = (channels == 1) ? CUBEB_LAYOUT_MONO : CUBEB_LAYOUT_STEREO;
        params.prefs = CUBEB_STREAM_PREF_NONE;

        std::uint32_t minimum_latency;
#ifdef EKA2L1_PLATFORM_ANDROID
        minimum_latency = 256;
#else
        minimum_latency = 100 * sample_rate / 1000; // Firefox default

        if (cubeb_get_min_latency(context, &params, &minimum_latency) != CUBEB_OK) {
            LOG_ERROR(DRIVER_AUD, "Error trying to get minimum latency. Use default");
        }
#endif

#ifdef EKA2L1_PLATFORM_ANDROID
        minimum_latency = common::max<std::uint32_t>(256U, minimum_latency);
#endif

        const auto result = cubeb_stream_init(context, &stream_, "EKA2L1 StreamPeam",
            nullptr, is_recording ? &params : nullptr, nullptr, is_recording ? nullptr : &params, minimum_latency,
            data_callback_redirector, state_callback_redirector, this);

        if (result != CUBEB_OK) {
            const char *trace_path = std::getenv("EKA2L1_PCM_TRACE");
            if (!is_recording && trace_path && *trace_path) {
                software_fallback_ = true;
                LOG_INFO(DRIVER_AUD, "PCMTRACE: cubeb unavailable; using software-clocked output stream");
                return;
            }

            LOG_CRITICAL(DRIVER_AUD, "Error trying to initialize cubeb stream!");
            return;
        }
    }

    cubeb_audio_stream_base::~cubeb_audio_stream_base() {
        stop_impl();
        if (stream_) {
            cubeb_stream_destroy(stream_);
        }
    }

    std::size_t cubeb_audio_stream_base::call_callback(std::int16_t *output_buffer, const long frames) {
        if (should_stream_idle()) {
            std::memset(output_buffer, 0, frames * internal_channels_ * sizeof(std::int16_t));
            idled_frames_ += static_cast<std::uint64_t>(frames);
  
            return static_cast<std::size_t>(frames);
        }

        return callback_(output_buffer, frames);
    }
    
    bool cubeb_audio_stream_base::current_frame_position_impl(std::uint64_t *pos) {
        if (!pos) {
            return false;
        }

        if (software_fallback_) {
            *pos = fallback_frames_.load();
            return true;
        }

        if (!stream_ || cubeb_stream_get_position(stream_, pos) != CUBEB_OK) {
            return false;
        }

        if (idled_frames_ >= *pos) {
            *pos = 0;
        } else {
            *pos -= idled_frames_;
        }

        return true;
    }

    bool cubeb_audio_stream_base::start_impl() {
        if (in_action_) {
            // A real cubeb output may be started again after it has drained. The
            // software fallback has a finished worker thread in that state, so
            // reap it before creating the next clocking worker.
            if (software_fallback_ && !fallback_running_.load()) {
                if (fallback_thread_.joinable()) {
                    fallback_thread_.join();
                }
                in_action_ = false;
            } else {
                return true;
            }
        }

        if (software_fallback_) {
            fallback_running_.store(true);
            in_action_ = true;
            fallback_thread_ = std::thread([this]() {
                const std::size_t block_frames = common::max<std::size_t>(1, fallback_sample_rate_ / 100);
                std::vector<std::int16_t> buffer(block_frames * internal_channels_);

                while (fallback_running_.load()) {
                    const auto begin = std::chrono::steady_clock::now();

                    if (should_stream_idle()) {
                        std::memset(buffer.data(), 0, buffer.size() * sizeof(std::int16_t));
                        const auto block_time = std::chrono::microseconds(
                            static_cast<std::int64_t>(frames_to_microseconds(block_frames, fallback_sample_rate_)));
                        const auto elapsed = std::chrono::steady_clock::now() - begin;
                        if (elapsed < block_time) {
                            std::this_thread::sleep_for(block_time - elapsed);
                        }
                        continue;
                    }

                    const std::size_t supplied = callback_(buffer.data(), block_frames);
                    fallback_frames_.fetch_add(supplied);

                    // cubeb treats a short data callback as end-of-stream and stops
                    // requesting data after those final frames have drained. Doing
                    // the same here is important for Symbian completion callbacks:
                    // repeatedly calling a source after EOF can signal completion
                    // more than once and leave game-side audio sequencing stuck.
                    const auto played_time = std::chrono::microseconds(
                        static_cast<std::int64_t>(frames_to_microseconds(supplied, fallback_sample_rate_)));
                    const auto elapsed = std::chrono::steady_clock::now() - begin;
                    if (elapsed < played_time) {
                        std::this_thread::sleep_for(played_time - elapsed);
                    }

                    if (supplied < block_frames) {
                        LOG_INFO(DRIVER_AUD, "PCMTRACE: software-clocked output drained after {} frames", fallback_frames_.load());
                        fallback_running_.store(false);
                        break;
                    }
                }
            });

            return true;
        }

        if (!stream_) {
            return false;
        }

        if (cubeb_stream_start(stream_) == CUBEB_OK) {
            in_action_ = true;
            idled_frames_ = 0;

            return true;
        }

        return false;
    }

    bool cubeb_audio_stream_base::stop_impl() {
        if (software_fallback_) {
            fallback_running_.store(false);
            if (fallback_thread_.joinable()) {
                fallback_thread_.join();
            }
            in_action_ = false;
            return true;
        }

        if (!in_action_) {
            return true;
        }

        if (stream_ && cubeb_stream_stop(stream_) == CUBEB_OK) {
            in_action_ = false;
            return true;
        }

        return false;
    }

    cubeb_audio_output_stream::cubeb_audio_output_stream(audio_driver *driver, cubeb *context, const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback)
        : audio_output_stream(driver, sample_rate, channels)
        , cubeb_audio_stream_base(context, sample_rate, channels, callback, false)
        , pausing_(false)
        , volume_(1.0f) {
    }

    cubeb_audio_output_stream::~cubeb_audio_output_stream() {
        stop();
    }

    bool cubeb_audio_output_stream::should_stream_idle() {
        return (pausing_ || driver_->suspending());
    }

    bool cubeb_audio_output_stream::start() {
        if (pausing_) {
            pausing_ = false;
            return true;
        }

        return cubeb_audio_stream_base::start_impl();
    }

    bool cubeb_audio_output_stream::stop() {
        if (cubeb_audio_stream_base::stop_impl()) {
            pausing_ = false;
            return true;
        }

        return false;
    }

    void cubeb_audio_output_stream::pause() {
        pausing_ = true;
    }

    bool cubeb_audio_output_stream::is_playing() {
        return in_action_;
    }

    bool cubeb_audio_output_stream::is_pausing() {
        return pausing_;
    }

    bool cubeb_audio_output_stream::set_volume(const float volume) {
        if (software_fallback_) {
            volume_ = volume;
            return true;
        }

        if (stream_ && cubeb_stream_set_volume(stream_, volume * static_cast<float>(driver_->master_volume() / 100.0f)) == CUBEB_OK) {
            volume_ = volume;
            return true;
        }

        return false;
    }

    float cubeb_audio_output_stream::get_volume() const {
        return volume_;
    }

    bool cubeb_audio_output_stream::current_frame_position(std::uint64_t *pos) {
        return current_frame_position_impl(pos);
    }

    cubeb_audio_input_stream::cubeb_audio_input_stream(audio_driver *driver, cubeb *context, const std::uint32_t sample_rate, const std::uint8_t channels,
        data_callback callback)
        : audio_input_stream(driver, sample_rate, channels)
        , cubeb_audio_stream_base(context, sample_rate, channels, callback, true) {
    }

    cubeb_audio_input_stream::~cubeb_audio_input_stream() {
        stop();
    }

    bool cubeb_audio_input_stream::start() {
        return cubeb_audio_stream_base::start_impl();
    }

    bool cubeb_audio_input_stream::stop() {
        return cubeb_audio_stream_base::stop_impl();
    }

    bool cubeb_audio_input_stream::is_recording() {
        return in_action_;
    }

    bool cubeb_audio_input_stream::current_frame_position(std::uint64_t *pos) {
        return cubeb_audio_stream_base::current_frame_position_impl(pos);
    }

    bool cubeb_audio_input_stream::should_stream_idle() {
        return driver_->suspending();
    }
}
