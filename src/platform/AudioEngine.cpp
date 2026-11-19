// MSentara — cross-platform 2D/3D animation studio
// Copyright (C) 2026 The MSentara Authors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "platform/AudioEngine.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "miniaudio.h"

namespace ms {
namespace {

constexpr std::uint32_t kOutputChannels = 2;

}  // namespace

struct AudioEngine::Impl {
    AudioClip clip;
    std::string path;

    ma_device device{};
    bool device_ok = false;
    std::string device_error;

    // Shared with the audio thread. The clip buffer itself is only replaced
    // while the device is stopped, so the callback can read it without a lock.
    std::atomic<bool> playing{false};
    std::atomic<std::uint64_t> cursor{0};  // frames into clip.samples
    std::uint64_t length_frames = 0;

    // Advances only when there is no device, so the timeline cursor still
    // moves on a headless machine.
    double virtual_seconds = 0.0;

    // Set when MSENTARA_AUDIO=0/null, or when the device failed to open.
    bool device_disabled = false;

    static void data_callback(ma_device* dev, void* out, const void* /*in*/,
                              ma_uint32 frame_count) {
        auto* self = static_cast<Impl*>(dev->pUserData);
        auto* dst = static_cast<float*>(out);
        const std::size_t total = static_cast<std::size_t>(frame_count) *
                                  kOutputChannels;

        if (self == nullptr || !self->playing.load(std::memory_order_relaxed) ||
            self->clip.samples.empty()) {
            std::fill(dst, dst + total, 0.0f);
            return;
        }

        const std::size_t len = self->clip.samples.size();
        std::uint64_t pos = self->cursor.load(std::memory_order_relaxed);
        for (ma_uint32 i = 0; i < frame_count; ++i) {
            float sample = 0.0f;
            if (pos < len) {
                sample = self->clip.samples[static_cast<std::size_t>(pos)];
                ++pos;
            }
            for (std::uint32_t c = 0; c < kOutputChannels; ++c) {
                *dst++ = sample;
            }
        }
        self->cursor.store(pos, std::memory_order_relaxed);
        if (pos >= len) {
            // End of clip: stop here. The main thread notices in update() and
            // stops the device; the callback must not do that itself.
            self->playing.store(false, std::memory_order_relaxed);
        }
    }

    bool start_device() {
        if (device_ok) {
            return true;
        }
        if (device_disabled) {
            return false;
        }
        // MSENTARA_AUDIO=0 or "null" forces the virtual-clock path. That keeps
        // CI and headless exports silent and, on Linux, stops miniaudio's ALSA
        // backend from probing (and complaining about) absent sound cards.
        if (const char* env = std::getenv("MSENTARA_AUDIO")) {
            const std::string mode(env);
            if (mode == "0" || mode == "null" || mode == "off") {
                device_disabled = true;
                device_error = "audio disabled by MSENTARA_AUDIO";
                return false;
            }
        }
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = kOutputChannels;
        cfg.sampleRate = clip.sample_rate;
        cfg.dataCallback = &Impl::data_callback;
        cfg.pUserData = this;

        const ma_result rc = ma_device_init(nullptr, &cfg, &device);
        if (rc != MA_SUCCESS) {
            device_error = "audio device unavailable (" +
                           std::string(ma_result_description(rc)) + ")";
            return false;
        }
        device_ok = true;
        return true;
    }

    void shutdown_device() {
        if (device_ok) {
            ma_device_uninit(&device);
            device_ok = false;
        }
    }
};

AudioEngine::AudioEngine() : impl_(new Impl()) {}

AudioEngine::~AudioEngine() {
    impl_->shutdown_device();
    delete impl_;
}

AudioClip AudioEngine::decode_file(const std::string& path) {
    AudioClip clip;

    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS) {
        return clip;
    }

    ma_uint32 channels = 0;
    ma_uint32 sample_rate = 0;
    ma_decoder_get_data_format(&decoder, nullptr, &channels, &sample_rate,
                               nullptr, 0);

    ma_uint64 frame_count = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frame_count) !=
            MA_SUCCESS ||
        frame_count == 0) {
        ma_decoder_uninit(&decoder);
        return clip;
    }

    clip.samples.resize(static_cast<std::size_t>(frame_count));
    clip.sample_rate = sample_rate;

    ma_uint64 total_read = 0;
    while (total_read < frame_count) {
        const ma_uint64 remaining = frame_count - total_read;
        ma_uint64 got = 0;
        const ma_result rc = ma_decoder_read_pcm_frames(
            &decoder, clip.samples.data() + total_read, remaining, &got);
        if (rc != MA_SUCCESS || got == 0) {
            break;
        }
        total_read += got;
    }
    ma_decoder_uninit(&decoder);

    // A truncated read still yields a usable clip; an empty one does not.
    clip.samples.resize(static_cast<std::size_t>(total_read));
    if (clip.samples.empty()) {
        clip.sample_rate = 0;
    }
    return clip;
}

bool AudioEngine::load(const std::string& path) {
    AudioClip clip = decode_file(path);
    if (!clip.valid()) {
        return false;
    }
    stop();
    impl_->path = path;
    set_clip(std::move(clip));
    return true;
}

void AudioEngine::set_clip(AudioClip clip) {
    stop();
    impl_->shutdown_device();
    impl_->clip = std::move(clip);
    impl_->length_frames = impl_->clip.samples.size();
    impl_->cursor.store(0, std::memory_order_relaxed);
    impl_->virtual_seconds = 0.0;
}

void AudioEngine::play() {
    if (!impl_->clip.valid()) {
        return;
    }
    // Restart from the top when the cursor already sits at the end.
    if (impl_->cursor.load(std::memory_order_relaxed) >= impl_->length_frames) {
        seek_seconds(0.0);
    }
    if (impl_->start_device()) {
        if (ma_device_start(&impl_->device) != MA_SUCCESS) {
            impl_->device_error = "audio device failed to start";
            impl_->shutdown_device();
        }
    }
    impl_->playing.store(true, std::memory_order_relaxed);
}

void AudioEngine::pause() {
    impl_->playing.store(false, std::memory_order_relaxed);
    if (impl_->device_ok) {
        ma_device_stop(&impl_->device);
    }
}

void AudioEngine::stop() {
    impl_->playing.store(false, std::memory_order_relaxed);
    if (impl_->device_ok) {
        ma_device_stop(&impl_->device);
    }
    impl_->cursor.store(0, std::memory_order_relaxed);
    impl_->virtual_seconds = 0.0;
}

void AudioEngine::seek_seconds(double seconds) {
    if (!impl_->clip.valid()) {
        return;
    }
    const double dur = impl_->clip.duration_seconds();
    seconds = std::max(0.0, std::min(seconds, dur));
    const auto frame = static_cast<std::uint64_t>(
        std::llround(seconds * static_cast<double>(impl_->clip.sample_rate)));
    impl_->cursor.store(std::min(frame, impl_->length_frames),
                        std::memory_order_relaxed);
    impl_->virtual_seconds = seconds;
}

bool AudioEngine::is_playing() const {
    return impl_->playing.load(std::memory_order_relaxed);
}

double AudioEngine::position_seconds() const {
    if (impl_->device_ok) {
        const std::uint64_t pos = impl_->cursor.load(std::memory_order_relaxed);
        return impl_->clip.sample_rate > 0
                   ? static_cast<double>(pos) /
                         static_cast<double>(impl_->clip.sample_rate)
                   : 0.0;
    }
    return impl_->virtual_seconds;
}

double AudioEngine::duration_seconds() const {
    return impl_->clip.duration_seconds();
}

bool AudioEngine::has_device() const { return impl_->device_ok; }

const std::string& AudioEngine::device_error() const {
    return impl_->device_error;
}

const AudioClip& AudioEngine::clip() const { return impl_->clip; }

const std::string& AudioEngine::path() const { return impl_->path; }

void AudioEngine::update(double delta_seconds) {
    // With a real device the callback owns the cursor. Without one, integrate
    // wall time so the timeline still advances.
    if (impl_->device_ok) {
        if (!impl_->playing.load(std::memory_order_relaxed) &&
            impl_->cursor.load(std::memory_order_relaxed) >=
                impl_->length_frames) {
            ma_device_stop(&impl_->device);
        }
        return;
    }

    if (!impl_->playing.load(std::memory_order_relaxed)) {
        return;
    }
    const double dur = impl_->clip.duration_seconds();
    impl_->virtual_seconds += delta_seconds;
    if (impl_->virtual_seconds >= dur) {
        impl_->virtual_seconds = dur;
        impl_->playing.store(false, std::memory_order_relaxed);
    }
}

}  // namespace ms
