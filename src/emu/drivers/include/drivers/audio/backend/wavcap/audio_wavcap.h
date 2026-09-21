/*
 * Copyright (c) 2025 EKA2L1 Team.
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

#include <drivers/audio/audio.h>
#include <drivers/audio/stream.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace eka2l1::drivers {
    /**
     * @brief An output driver that writes to .wav files instead of a sound device.
     *
     * A headless machine has no device for cubeb to open, which leaves no way to tell
     * whether a title is producing sound at all. This driver pulls each stream on its
     * own thread, paced by the wall clock exactly as a device would, and writes what
     * comes back to one file per stream -- so a test can look at the audio itself
     * rather than guess from the log.
     */
    struct wavcap_audio_driver : public audio_driver {
    private:
        std::string directory_;
        std::atomic<int> next_stream_index_;

    public:
        explicit wavcap_audio_driver(const std::string &directory,
            const std::uint32_t initial_master_volume = 100,
            const player_type preferred_midi_backend = player_type_tsf);

        ~wavcap_audio_driver() override = default;

        std::unique_ptr<audio_output_stream> new_output_stream(const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback) override;

        std::unique_ptr<audio_input_stream> new_input_stream(const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback) override;

        std::uint32_t native_sample_rate() override;

        std::string next_path(const std::uint32_t sample_rate, const std::uint8_t channels);
    };

    struct wavcap_audio_output_stream : public audio_output_stream {
    private:
        data_callback callback_;
        std::string path_;
        FILE *file_;

        std::thread pump_;
        std::mutex lock_;
        std::atomic<bool> running_;
        std::atomic<bool> playing_;
        std::atomic<bool> pausing_;

        std::uint64_t frames_written_;
        float volume_;

        void pump();
        void write_header(const std::uint32_t data_bytes);

    public:
        explicit wavcap_audio_output_stream(audio_driver *driver, const std::string &path,
            const std::uint32_t sample_rate, const std::uint8_t channels, data_callback callback);

        ~wavcap_audio_output_stream() override;

        bool start() override;
        bool stop() override;
        void pause() override;

        bool is_playing() override;
        bool is_pausing() override;

        bool set_volume(const float volume) override;
        float get_volume() const override;

        bool current_frame_position(std::uint64_t *pos) override;
    };
}
