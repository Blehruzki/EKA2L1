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

#include <drivers/audio/backend/wavcap/audio_wavcap.h>

#include <common/fileutils.h>
#include <common/log.h>
#include <common/path.h>

#include <chrono>
#include <cstring>
#include <vector>

namespace eka2l1::drivers {
    // One pull per 20ms, the granularity a device callback would run at.
    static constexpr std::uint32_t PUMP_INTERVAL_MS = 20;
    static constexpr std::uint32_t CAPTURE_NATIVE_RATE = 44100;

    wavcap_audio_driver::wavcap_audio_driver(const std::string &directory,
        const std::uint32_t initial_master_volume, const player_type preferred_midi_backend)
        : audio_driver(initial_master_volume, preferred_midi_backend)
        , directory_(directory)
        , next_stream_index_(0) {
        if (!directory_.empty() && !eka2l1::common::exists(directory_)) {
            eka2l1::common::create_directories(directory_);
        }

        LOG_INFO(DRIVER_AUD, "Audio is being captured to .wav files in {}", directory_);
    }

    std::string wavcap_audio_driver::next_path(const std::uint32_t sample_rate, const std::uint8_t channels) {
        const int index = next_stream_index_++;
        return eka2l1::add_path(directory_, fmt::format("stream{:02d}_{}hz_{}ch.wav", index,
            sample_rate, static_cast<int>(channels)));
    }

    std::unique_ptr<audio_output_stream> wavcap_audio_driver::new_output_stream(const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback) {
        return std::make_unique<wavcap_audio_output_stream>(this, next_path(sample_rate, channels),
            sample_rate, channels, callback);
    }

    std::unique_ptr<audio_input_stream> wavcap_audio_driver::new_input_stream(const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback) {
        // Nothing to record from; a title asking for input gets no stream, same as a
        // machine with no microphone.
        return nullptr;
    }

    std::uint32_t wavcap_audio_driver::native_sample_rate() {
        return CAPTURE_NATIVE_RATE;
    }

    wavcap_audio_output_stream::wavcap_audio_output_stream(audio_driver *driver, const std::string &path,
        const std::uint32_t sample_rate, const std::uint8_t channels, data_callback callback)
        : audio_output_stream(driver, sample_rate, channels)
        , callback_(callback)
        , path_(path)
        , file_(nullptr)
        , running_(false)
        , playing_(false)
        , pausing_(false)
        , frames_written_(0)
        , volume_(1.0f) {
        file_ = eka2l1::common::open_c_file(path_, "wb");

        if (!file_) {
            LOG_ERROR(DRIVER_AUD, "Unable to open {} to capture audio to", path_);
            return;
        }

        write_header(0);
    }

    wavcap_audio_output_stream::~wavcap_audio_output_stream() {
        stop();

        const std::lock_guard<std::mutex> guard(lock_);

        if (file_) {
            write_header(static_cast<std::uint32_t>(frames_written_ * channels * sizeof(std::int16_t)));
            fclose(file_);
            file_ = nullptr;
        }
    }

    void wavcap_audio_output_stream::write_header(const std::uint32_t data_bytes) {
        const std::uint32_t byte_rate = sample_rate * channels * 2;
        std::uint8_t header[44];

        auto put32 = [&](const int at, const std::uint32_t value) {
            header[at] = value & 0xFF;
            header[at + 1] = (value >> 8) & 0xFF;
            header[at + 2] = (value >> 16) & 0xFF;
            header[at + 3] = (value >> 24) & 0xFF;
        };
        auto put16 = [&](const int at, const std::uint16_t value) {
            header[at] = value & 0xFF;
            header[at + 1] = (value >> 8) & 0xFF;
        };

        std::memcpy(header, "RIFF", 4);
        put32(4, 36 + data_bytes);
        std::memcpy(header + 8, "WAVEfmt ", 8);
        put32(16, 16);
        put16(20, 1);
        put16(22, channels);
        put32(24, sample_rate);
        put32(28, byte_rate);
        put16(32, static_cast<std::uint16_t>(channels * 2));
        put16(34, 16);
        std::memcpy(header + 36, "data", 4);
        put32(40, data_bytes);

        const long resume = ftell(file_);
        fseek(file_, 0, SEEK_SET);
        fwrite(header, 1, sizeof(header), file_);

        if (resume > 0) {
            fseek(file_, resume, SEEK_SET);
        }
    }

    void wavcap_audio_output_stream::pump() {
        const std::size_t chunk = sample_rate * PUMP_INTERVAL_MS / 1000;
        std::vector<std::int16_t> buffer(chunk * channels);
        auto deadline = std::chrono::steady_clock::now();

        while (running_) {
            std::size_t got = 0;

            if (pausing_) {
                std::fill(buffer.begin(), buffer.end(), static_cast<std::int16_t>(0));
                got = chunk;
            } else {
                got = callback_(buffer.data(), chunk);

                if (got < chunk) {
                    std::fill(buffer.begin() + got * channels, buffer.end(), static_cast<std::int16_t>(0));
                }
            }

            const float gain = volume_ * static_cast<float>(driver_->master_volume()) / 100.0f;

            if (gain != 1.0f) {
                for (std::int16_t &sample : buffer) {
                    sample = static_cast<std::int16_t>(sample * gain);
                }
            }

            {
                const std::lock_guard<std::mutex> guard(lock_);

                if (file_) {
                    fwrite(buffer.data(), sizeof(std::int16_t), buffer.size(), file_);
                    frames_written_ += chunk;

                    // Keep the sizes current: a captured run usually ends by killing the
                    // emulator, which never reaches the destructor that would patch them.
                    write_header(static_cast<std::uint32_t>(frames_written_ * channels
                        * sizeof(std::int16_t)));
                }
            }

            deadline += std::chrono::milliseconds(PUMP_INTERVAL_MS);
            std::this_thread::sleep_until(deadline);
        }
    }

    bool wavcap_audio_output_stream::start() {
        if (running_) {
            return true;
        }

        running_ = true;
        playing_ = true;
        pausing_ = false;
        pump_ = std::thread([this]() { pump(); });

        return true;
    }

    bool wavcap_audio_output_stream::stop() {
        if (!running_) {
            return true;
        }

        running_ = false;
        playing_ = false;

        if (pump_.joinable()) {
            pump_.join();
        }

        return true;
    }

    void wavcap_audio_output_stream::pause() {
        pausing_ = true;
        playing_ = false;
    }

    bool wavcap_audio_output_stream::is_playing() {
        return playing_;
    }

    bool wavcap_audio_output_stream::is_pausing() {
        return pausing_;
    }

    bool wavcap_audio_output_stream::set_volume(const float volume) {
        volume_ = volume;
        return true;
    }

    float wavcap_audio_output_stream::get_volume() const {
        return volume_;
    }

    bool wavcap_audio_output_stream::current_frame_position(std::uint64_t *pos) {
        if (!pos) {
            return false;
        }

        *pos = frames_written_;
        return true;
    }
}
