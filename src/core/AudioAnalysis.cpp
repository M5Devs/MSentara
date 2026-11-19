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

#include "core/AudioAnalysis.hpp"

#include <algorithm>
#include <cmath>

namespace ms {

float AudioEnvelope::at(int frame) const {
    if (frame < 0 || frame >= static_cast<int>(levels.size())) {
        return 0.0f;
    }
    return levels[static_cast<std::size_t>(frame)];
}

AudioEnvelope compute_envelope(const std::vector<float>& samples,
                               std::uint32_t sample_rate,
                               double frames_per_second) {
    AudioEnvelope env;
    env.frames_per_second = frames_per_second;
    if (samples.empty() || sample_rate == 0 || frames_per_second <= 0.0) {
        return env;
    }

    const double samples_per_frame =
        static_cast<double>(sample_rate) / frames_per_second;
    const int frame_count = static_cast<int>(
        std::ceil(static_cast<double>(samples.size()) / samples_per_frame));
    if (frame_count <= 0) {
        return env;
    }

    env.levels.assign(static_cast<std::size_t>(frame_count), 0.0f);

    float peak = 0.0f;
    for (int f = 0; f < frame_count; ++f) {
        const std::size_t begin = static_cast<std::size_t>(
            std::llround(static_cast<double>(f) * samples_per_frame));
        std::size_t end = static_cast<std::size_t>(
            std::llround(static_cast<double>(f + 1) * samples_per_frame));
        end = std::min(end, samples.size());
        if (begin >= end) {
            continue;
        }

        double sum_sq = 0.0;
        for (std::size_t i = begin; i < end; ++i) {
            const double s = static_cast<double>(samples[i]);
            sum_sq += s * s;
        }
        const auto rms = static_cast<float>(
            std::sqrt(sum_sq / static_cast<double>(end - begin)));
        env.levels[static_cast<std::size_t>(f)] = rms;
        peak = std::max(peak, rms);
    }

    // Normalise to [0, 1]. A silent clip stays all zeros rather than dividing
    // by zero.
    if (peak > 0.0f) {
        const float inv = 1.0f / peak;
        for (float& level : env.levels) {
            level = std::min(1.0f, level * inv);
        }
    }

    env.frame_count = frame_count;
    return env;
}

}  // namespace ms
