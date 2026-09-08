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

#pragma once

#include <cubeb/cubeb.h>
#include <drivers/audio/stream.h>

#include <atomic>
#include <thread>

namespace eka2l1::drivers {
    struct cubeb_audio_stream_base {
    protected:
        cubeb_stream *stream_;
        data_callback callback_;
        
        std::uint64_t idled_frames_;
        std::uint8_t internal_channels_;

        bool in_action_;

        // Validation-only fallback used when cubeb cannot create a host stream and
        // EKA2L1_PCM_TRACE is enabled. This keeps game-side audio callbacks clocked
        // without requiring ALSA/PulseAudio/cubeb output hardware.
        bool software_fallback_;
        std::uint32_t fallback_sample_rate_;
        std::atomic<bool> fallback_running_;
        std::atomic<std::uint64_t> fallback_frames_;
        std::thread fallback_thread_;

    protected:
        bool current_frame_position_impl(std::uint64_t *val);
        bool start_impl();
        bool stop_impl();

        virtual bool should_stream_idle() = 0;

    public:
        explicit cubeb_audio_stream_base(cubeb *context, const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback, bool is_recording = false);

        virtual ~cubeb_audio_stream_base();

        std::size_t call_callback(std::int16_t *output_buffer, const long frames);
    };

    struct cubeb_audio_output_stream : public audio_output_stream, public cubeb_audio_stream_base {
    private:
        data_callback callback_;
        bool pausing_;
        float volume_;

    protected:
        bool should_stream_idle() override;

    public:
        explicit cubeb_audio_output_stream(audio_driver *driver, cubeb *context, const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback);

        ~cubeb_audio_output_stream() override;

        bool start() override;
        bool stop() override;
        void pause() override;

        bool is_playing() override;
        bool is_pausing() override;

        bool set_volume(const float volume) override;
        float get_volume() const override;
        
        bool current_frame_position(std::uint64_t *pos) override;
    };

    struct cubeb_audio_input_stream : public audio_input_stream, public cubeb_audio_stream_base {
    protected:
        cubeb_stream *stream_;
        data_callback callback_;

        std::uint64_t idled_frames_;

    protected:
        bool should_stream_idle() override;

    public:
        explicit cubeb_audio_input_stream(audio_driver *driver, cubeb *context, const std::uint32_t sample_rate, const std::uint8_t channels,
            data_callback callback);
        virtual ~cubeb_audio_input_stream();

        bool start() override;
        bool stop() override;

        bool is_recording() override;
        bool current_frame_position(std::uint64_t *pos) override;
    };
}
