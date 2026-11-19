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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ms {

// Decoded audio, in the shape the rest of the app wants it: mono float samples
// plus the source's sample rate. Every importer normalises to this so playback,
// waveform drawing and lip-sync analysis share one representation.
struct AudioClip {
    std::vector<float> samples;  // mono, nominally [-1, 1]
    std::uint32_t sample_rate = 0;

    bool valid() const { return sample_rate > 0 && !samples.empty(); }
    double duration_seconds() const {
        return sample_rate > 0
                   ? static_cast<double>(samples.size()) /
                         static_cast<double>(sample_rate)
                   : 0.0;
    }
};

// Plays back an AudioClip through miniaudio, and decodes files into one.
//
// Audio is optional: a headless CI machine has no output device, and the web
// build has none until the user clicks. Every method therefore degrades to a
// no-op instead of failing hard, and is_playing()/position_seconds() still
// advance so the timeline cursor moves even with no sound.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Decodes an audio file (wav/mp3/flac/ogg) into a mono AudioClip. Returns
    // an invalid clip when the file is missing or undecodable.
    static AudioClip decode_file(const std::string& path);

    // Loads and decodes a file, replacing any current clip and stopping
    // playback. Returns false when the file cannot be decoded.
    bool load(const std::string& path);

    // Installs an already-decoded clip (e.g. from a project that embedded it).
    void set_clip(AudioClip clip);

    void play();
    void pause();
    void stop();

    // Moves the playback cursor. Clamped to [0, duration]. Works while playing
    // or paused.
    void seek_seconds(double seconds);

    bool is_playing() const;
    // Current playhead in seconds. With no audio device this advances from a
    // wall clock while playing, so the timeline still follows the cursor.
    double position_seconds() const;
    double duration_seconds() const;

    // True when a real output device is running. False on headless machines or
    // when the device failed to open; playback then runs on a virtual clock.
    bool has_device() const;
    // Human-readable reason the device is unavailable (empty when it is fine).
    const std::string& device_error() const;

    const AudioClip& clip() const;
    const std::string& path() const;

    // Advances the virtual clock. Call once per frame. A real device keeps its
    // own position, so this is a no-op then; with no device it integrates the
    // elapsed wall time so position_seconds() still moves.
    void update(double delta_seconds);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace ms
