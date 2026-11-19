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
#include <vector>

namespace ms {

// A loudness envelope sampled once per animation frame. The timeline draws it
// as the waveform strip and the lip-sync generator reads it as mouth openness.
struct AudioEnvelope {
    // One value per frame bucket, nominally in [0, 1]. 0 is silence, 1 is the
    // loudest bucket in the clip.
    std::vector<float> levels;

    // Frames per second the envelope was bucketed at, and the clip length in
    // frames (so a caller knows how many timeline frames the audio covers).
    double frames_per_second = 0.0;
    int frame_count = 0;

    bool empty() const { return levels.empty(); }
    // Level for a frame, clamped to the valid range. Returns 0 off the ends.
    float at(int frame) const;
};

// Reduces mono PCM to a per-frame loudness envelope. This lives in core because
// it is pure data processing: no device, no platform code, and therefore unit
// testable without audio hardware.
//
// The envelope is RMS per bucket, then normalised so the loudest bucket is 1.
// RMS tracks perceived loudness far better than peak, which is what makes the
// waveform readable and the lip-sync plausible.
AudioEnvelope compute_envelope(const std::vector<float>& samples,
                               std::uint32_t sample_rate,
                               double frames_per_second = 30.0);

}  // namespace ms
