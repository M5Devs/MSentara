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

// Tests for the audio layer. The envelope logic is pure core code and needs no
// hardware. The AudioEngine transport is also exercised here: it must work
// with no output device (as on a CI machine), decoding a file and advancing a
// virtual playhead. We synthesise a WAV in a temp file so the decoder reads a
// real file rather than a mock.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "TestCommon.hpp"

#include "core/AudioAnalysis.hpp"
#include "platform/AudioEngine.hpp"

using namespace ms;
using namespace ms::test;

namespace {

// A one-second mono 8 kHz tone that is silent for its first half, then a
// steady 440 Hz sine at half full scale. That shape makes the envelope easy to
// assert: near-zero buckets first, then a plateau.
std::vector<float> make_test_samples(std::uint32_t rate) {
    std::vector<float> samples(rate, 0.0f);
    for (std::uint32_t i = rate / 2; i < rate; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(rate);
        samples[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * t));
    }
    return samples;
}

void test_envelope_shape() {
    const std::uint32_t rate = 8000;
    const std::vector<float> samples = make_test_samples(rate);
    const AudioEnvelope env = compute_envelope(samples, rate, 30.0);

    // One second at 30 fps is 30 buckets (the ceil of 30.0 exact).
    MS_CHECK(env.frame_count == 30);
    MS_CHECK(env.levels.size() == 30u);
    MS_CHECK(env.frames_per_second == 30.0);

    // The first half is silent; the second half is loud. Check the boundary
    // buckets rather than every one to stay robust to bucket rounding.
    MS_CHECK(env.at(2) < 0.01f);
    MS_CHECK(env.at(12) < 0.01f);
    MS_CHECK(env.at(20) > 0.5f);
    MS_CHECK(env.at(28) > 0.5f);

    // Normalised: the loudest bucket is exactly 1.
    float peak = 0.0f;
    for (float v : env.levels) {
        peak = std::max(peak, v);
    }
    MS_CHECK(nearly(peak, 1.0f, 1e-5f));
}

void test_envelope_at_clamps() {
    const std::uint32_t rate = 8000;
    const AudioEnvelope env =
        compute_envelope(make_test_samples(rate), rate, 30.0);
    MS_CHECK(env.at(-1) == 0.0f);
    MS_CHECK(env.at(1000) == 0.0f);
    MS_CHECK(env.at(0) == env.levels[0]);
}

void test_envelope_empty_and_silent() {
    // Empty input yields an empty envelope, not a crash or a division by zero.
    const AudioEnvelope empty = compute_envelope({}, 8000, 30.0);
    MS_CHECK(empty.empty());
    MS_CHECK(empty.frame_count == 0);

    // A clip of pure silence stays all zeros rather than normalising up.
    const std::vector<float> silence(8000, 0.0f);
    const AudioEnvelope silent = compute_envelope(silence, 8000, 30.0);
    MS_CHECK(silent.frame_count == 30);
    for (float v : silent.levels) {
        MS_CHECK(v == 0.0f);
    }
}

void test_envelope_frame_rate_independence() {
    // The same one-second clip bucketed at 60 fps must yield ~60 buckets and
    // the same silent-then-loud split.
    const std::uint32_t rate = 8000;
    const std::vector<float> samples = make_test_samples(rate);
    const AudioEnvelope env = compute_envelope(samples, rate, 60.0);
    MS_CHECK(env.frame_count == 60);
    MS_CHECK(env.at(4) < 0.01f);
    MS_CHECK(env.at(50) > 0.5f);
}

// Writes a mono 16-bit PCM WAV with miniaudio's encoder-free path: a plain
// header followed by little-endian samples. Kept hand-rolled so the test does
// not depend on the encoder being compiled in.
bool write_wav(const std::string& path, const std::vector<float>& samples,
               std::uint32_t rate) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        return false;
    }

    const std::uint16_t channels = 1;
    const std::uint16_t bits = 16;
    const std::uint32_t data_bytes =
        static_cast<std::uint32_t>(samples.size()) * 2u;
    const std::uint32_t byte_rate = rate * channels * bits / 8u;
    const std::uint16_t block_align = channels * bits / 8u;

    auto u32 = [&](std::uint32_t v) {
        std::uint8_t b[4] = {static_cast<std::uint8_t>(v & 0xFF),
                             static_cast<std::uint8_t>((v >> 8) & 0xFF),
                             static_cast<std::uint8_t>((v >> 16) & 0xFF),
                             static_cast<std::uint8_t>((v >> 24) & 0xFF)};
        std::fwrite(b, 1, 4, f);
    };
    auto u16 = [&](std::uint16_t v) {
        std::uint8_t b[2] = {static_cast<std::uint8_t>(v & 0xFF),
                             static_cast<std::uint8_t>((v >> 8) & 0xFF)};
        std::fwrite(b, 1, 2, f);
    };

    std::fwrite("RIFF", 1, 4, f);
    u32(36u + data_bytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16);            // fmt chunk size
    u16(1);             // PCM
    u16(channels);
    u32(rate);
    u32(byte_rate);
    u16(block_align);
    u16(bits);
    std::fwrite("data", 1, 4, f);
    u32(data_bytes);
    for (float s : samples) {
        float clamped = std::max(-1.0f, std::min(1.0f, s));
        const auto v = static_cast<std::int16_t>(std::lround(clamped * 32767.0f));
        u16(static_cast<std::uint16_t>(v));
    }
    std::fclose(f);
    return true;
}

void test_decode_and_transport() {
    // Force the virtual-clock path: deterministic, silent, and it avoids ALSA
    // probing absent sound cards on CI.
    setenv("MSENTARA_AUDIO", "0", 1);

    const std::uint32_t rate = 8000;
    const std::vector<float> samples = make_test_samples(rate);
    const std::string path = "test_audio_tone.wav";
    if (!write_wav(path, samples, rate)) {
        MS_CHECK_MSG(false, "could not write temp wav");
        return;
    }

    // Decoding yields a clip of the right length and rate.
    const AudioClip clip = AudioEngine::decode_file(path);
    MS_CHECK(clip.valid());
    MS_CHECK(clip.sample_rate == rate);
    MS_CHECK(std::abs(static_cast<int>(clip.samples.size()) -
                      static_cast<int>(samples.size())) <= 2);
    MS_CHECK(nearly(static_cast<float>(clip.duration_seconds()), 1.0f, 1e-2f));

    // A missing file decodes to an invalid clip rather than crashing.
    MS_CHECK(!AudioEngine::decode_file("no_such_audio_file.wav").valid());

    AudioEngine engine;
    MS_CHECK(engine.load(path));
    MS_CHECK(engine.duration_seconds() > 0.9);
    MS_CHECK(engine.path() == path);
    MS_CHECK(!engine.is_playing());

    // Transport works with or without a device. Seek clamps to the clip.
    engine.seek_seconds(0.5);
    MS_CHECK(nearly(static_cast<float>(engine.position_seconds()), 0.5f, 2e-2f));
    engine.seek_seconds(-10.0);
    MS_CHECK(engine.position_seconds() == 0.0);
    engine.seek_seconds(100.0);
    MS_CHECK(engine.position_seconds() <= engine.duration_seconds() + 1e-6);

    // Playback advances the virtual clock: the device is disabled, so the
    // playhead must move with update() and stop cleanly.
    engine.seek_seconds(0.0);
    engine.play();
    MS_CHECK(engine.is_playing());
    MS_CHECK(!engine.has_device());
    MS_CHECK(!engine.device_error().empty());
    for (int i = 0; i < 10; ++i) {
        engine.update(0.01);
    }
    MS_CHECK(engine.position_seconds() > 0.0);
    engine.stop();
    MS_CHECK(!engine.is_playing());
    MS_CHECK(engine.position_seconds() == 0.0);

    // Loading a bad path leaves the previous clip in place and reports failure.
    MS_CHECK(!engine.load("no_such_audio_file.wav"));
    MS_CHECK(engine.duration_seconds() > 0.9);

    std::remove(path.c_str());
}

}  // namespace

int main() {
    test_envelope_shape();
    test_envelope_at_clamps();
    test_envelope_empty_and_silent();
    test_envelope_frame_rate_independence();
    test_decode_and_transport();
    return summary("audio");
}
