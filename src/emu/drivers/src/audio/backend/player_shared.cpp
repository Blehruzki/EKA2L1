/*
 * Copyright (c) 2020 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <drivers/audio/audio.h>
#include <drivers/audio/backend/player_shared.h>

#include <common/algorithm.h>
#include <common/cvt.h>
#include <common/log.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace eka2l1::drivers {
    static const char *asphalt3_pcm_trace_path() {
        const char *path = std::getenv("EKA2L1_PCM_TRACE");
        return (path && *path) ? path : nullptr;
    }

    static void asphalt3_pcm_trace(const std::int16_t *data, const std::size_t frames, const std::uint32_t channels) {
        const char *path = asphalt3_pcm_trace_path();
        if (!path || !data || !frames || !channels)
            return;

        FILE *out = std::fopen(path, "ab");
        if (!out)
            return;
        std::fwrite(data, sizeof(std::int16_t), frames * channels, out);
        std::fclose(out);
    }

    // Hardware-independent output stream used only when EKA2L1_PCM_TRACE is set.
    // It clocks the normal decoder callback from a worker thread, so validation does
    // not depend on cubeb/ALSA/PulseAudio successfully opening a host audio device.
    class asphalt3_trace_output_stream final : public audio_output_stream {
    public:
        asphalt3_trace_output_stream(audio_driver *driver, const std::uint32_t rate,
            const std::uint8_t channel_count, data_callback callback)
            : audio_output_stream(driver, rate, channel_count), callback_(std::move(callback)),
              running_(false), paused_(false), frames_played_(0), volume_(1.0f) {
        }

        ~asphalt3_trace_output_stream() override {
            stop();
        }

        bool start() override {
            if (running_.load()) {
                paused_.store(false);
                return true;
            }

            running_.store(true);
            paused_.store(false);
            worker_ = std::thread([this]() {
                // 10 ms blocks are small enough to make pause/resume boundaries precise
                // while keeping overhead low.
                const std::size_t block_frames = std::max<std::size_t>(1, sample_rate / 100);
                std::vector<std::int16_t> buffer(block_frames * channels);
                const auto block_time = std::chrono::microseconds(
                    static_cast<std::int64_t>(frames_to_microseconds(block_frames, sample_rate)));

                while (running_.load()) {
                    if (paused_.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }

                    const auto begin = std::chrono::steady_clock::now();
                    const std::size_t supplied = callback_(buffer.data(), block_frames);
                    frames_played_.fetch_add(supplied);

                    if (supplied == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }

                    const auto elapsed = std::chrono::steady_clock::now() - begin;
                    if (elapsed < block_time)
                        std::this_thread::sleep_for(block_time - elapsed);
                }
            });
            return true;
        }

        bool stop() override {
            running_.store(false);
            paused_.store(false);
            if (worker_.joinable())
                worker_.join();
            return true;
        }

        void pause() override {
            if (running_.load())
                paused_.store(true);
        }

        bool is_playing() override {
            return running_.load() && !paused_.load();
        }

        bool is_pausing() override {
            return running_.load() && paused_.load();
        }

        bool set_volume(const float volume) override {
            volume_.store(volume);
            return true;
        }

        float get_volume() const override {
            return volume_.load();
        }

        bool current_frame_position(std::uint64_t *pos) override {
            if (!pos)
                return false;
            *pos = frames_played_.load();
            return true;
        }

    private:
        data_callback callback_;
        std::thread worker_;
        std::atomic<bool> running_;
        std::atomic<bool> paused_;
        std::atomic<std::uint64_t> frames_played_;
        std::atomic<float> volume_;
    };

    std::size_t player_shared::data_supply_callback(std::int16_t *data, std::size_t size) {
        const std::lock_guard<std::mutex> guard(lock_);
        std::size_t frame_copied = 0;

        auto supply_stuff = [&]() {
            while ((frame_copied < size) && (!(flags_ & 1))) {
                if (data_.size() <= data_pointer_)
                    get_more_data();

                const std::size_t total_frame_left = (data_.size() - data_pointer_ + 1) / channels_ / sizeof(std::uint16_t);
                const std::size_t frame_to_copy = std::min<std::size_t>(total_frame_left, size - frame_copied);
                std::memcpy(data + frame_copied * channels_, data_.data() + data_pointer_, frame_to_copy * channels_ * sizeof(std::uint16_t));
                data_pointer_ += frame_to_copy * channels_ * sizeof(std::uint16_t);
                frame_copied += frame_to_copy;
            }
        };

        supply_stuff();

        if ((frame_copied < size) || (flags_ & 1)) {
            bool no_more_way = false;
            if (repeat_left_ == 0) {
                no_more_way = true;
            } else {
                if (repeat_left_ > 0)
                    repeat_left_ -= 1;
                reset_request();
                data_pointer_ = 0;
                flags_ = 0;
                const std::size_t silence_samples = freq_ * silence_micros_ / 1000000;
                data_.resize(silence_samples * sizeof(std::uint16_t) * channels_);
                std::fill(data_.begin(), data_.end(), 0);
                use_push_new_data_ = true;
                supply_stuff();
            }
            if (no_more_way && callback_)
                callback_(userdata_.data());
        }

        asphalt3_pcm_trace(data, frame_copied, channels_);
        return frame_copied;
    }

    bool player_shared::play() {
        if (output_stream_) {
            if (output_stream_->is_pausing()) {
                LOG_INFO(DRIVER_AUD, "PCMTRACE: resume existing audio stream");
                output_stream_->start();
                return true;
            }
            output_stream_->stop();
        }

        reset_request();
        if (!is_ready_to_play())
            return false;

        data_pointer_ = 0;
        flags_ = 0;
        data_.clear();

        const auto supply_callback = [this](std::int16_t *u1, std::size_t u2) {
            return data_supply_callback(u1, u2);
        };

        if (asphalt3_pcm_trace_path()) {
            output_stream_ = std::make_unique<asphalt3_trace_output_stream>(aud_, freq_, channels_, supply_callback);
            LOG_INFO(DRIVER_AUD, "PCMTRACE: using hardware-independent trace output stream");
        } else {
            output_stream_ = aud_->new_output_stream(freq_, channels_, supply_callback);
        }

        if (!output_stream_)
            return true;

        output_stream_->set_volume(static_cast<float>(volume_) / 10.0f);
        LOG_INFO(DRIVER_AUD, "PCMTRACE: start audio stream {} Hz {} ch", freq_, channels_);
        return output_stream_->start();
    }

    bool player_shared::stop() {
        LOG_INFO(DRIVER_AUD, "PCMTRACE: stop audio stream");
        if (output_stream_)
            return output_stream_->stop();
        return true;
    }

    void player_shared::pause() {
        LOG_INFO(DRIVER_AUD, "PCMTRACE: pause audio stream at {} us", position());
        if (output_stream_)
            output_stream_->pause();
    }

    bool player_shared::notify_any_done(finish_callback callback, std::uint8_t *data, const std::size_t data_size) {
        return player::notify_any_done(callback, data, data_size);
    }
    void player_shared::clear_notify_done() { return player::clear_notify_done(); }

    void player_shared::set_repeat(const std::int32_t repeat_times, const std::int64_t silence_intervals_micros) {
        LOG_INFO(DRIVER_AUD, "PCMTRACE: SetRepeats {} silence {} us", repeat_times, silence_intervals_micros);
        repeat_left_ = repeat_times;
        silence_micros_ = silence_intervals_micros < 0 ? 0 : silence_intervals_micros;
    }

    void player_shared::set_position(const std::uint64_t pos_in_us) {
        const std::lock_guard<std::mutex> guard(lock_);
        set_position_for_custom_format(pos_in_us);
    }
    bool player_shared::set_dest_freq(const std::uint32_t freq) { freq_ = freq; return true; }
    bool player_shared::set_dest_channel_count(const std::uint32_t cn) { channels_ = cn; return true; }
    bool player_shared::set_dest_encoding(const std::uint32_t enc) { encoding_ = enc; return true; }
    void player_shared::set_dest_container_format(const std::uint32_t confor) { format_ = confor; }

    bool player_shared::set_volume(const std::uint32_t vol) {
        const bool res = player::set_volume(vol);
        if (output_stream_ && res)
            output_stream_->set_volume(static_cast<float>(volume_) / static_cast<float>(max_volume()));
        return res;
    }

    std::uint32_t player_shared::get_dest_freq() { return freq_; }
    std::uint32_t player_shared::get_dest_channel_count() { return channels_; }
    std::uint32_t player_shared::get_dest_encoding() { return encoding_; }

    std::uint64_t player_shared::position() const {
        std::uint64_t pos_in_frames = 0;
        if (!output_stream_ || !freq_ || !output_stream_->current_frame_position(&pos_in_frames))
            return 0;
        return frames_to_microseconds(pos_in_frames, freq_);
    }

    player_shared::player_shared(audio_driver *driver)
        : aud_(driver), data_pointer_(0), flags_(0), repeat_left_(0), silence_micros_(0), pos_in_us_(0), use_push_new_data_(false) {}

    player_shared::~player_shared() {
        if (output_stream_)
            output_stream_->stop();
    }
}
