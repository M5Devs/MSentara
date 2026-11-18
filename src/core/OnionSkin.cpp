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

#include "core/OnionSkin.hpp"

#include <algorithm>
#include <cmath>

namespace ms {

std::vector<OnionFrame> onion_skin_window(const OnionSkinSettings& settings,
                                          int current_frame, int frame_start,
                                          int frame_end) {
    std::vector<OnionFrame> frames;
    if (!settings.enabled) {
        return frames;
    }
    if (frame_end < frame_start) {
        std::swap(frame_start, frame_end);
    }
    const int prev = std::max(settings.prev_frames, 0);
    const int next = std::max(settings.next_frames, 0);

    const auto in_range = [frame_start, frame_end](int frame) {
        return frame >= frame_start && frame <= frame_end;
    };

    // Past frames, farthest first so the nearest past frame draws last (on top
    // of the farther ones). A frame outside the timeline is simply skipped.
    for (int d = prev; d >= 1; --d) {
        const int frame = current_frame - d;
        if (in_range(frame)) {
            frames.push_back(OnionFrame{frame, -d});
        }
    }
    // Future frames, farthest first for the same reason.
    for (int d = next; d >= 1; --d) {
        const int frame = current_frame + d;
        if (in_range(frame)) {
            frames.push_back(OnionFrame{frame, d});
        }
    }
    return frames;
}

vec4 onion_ghost_tint(int offset, float opacity) {
    // Past = cool blue, future = warm orange. The base alpha is the setting;
    // it fades linearly with distance so the ghost nearest the current frame is
    // the most visible, which is what makes the spacing read.
    const bool past = offset < 0;
    const vec3 rgb = past ? vec3(0.45f, 0.65f, 1.0f)   // cool blue
                          : vec3(1.0f, 0.55f, 0.35f);  // warm red/orange
    const float distance = std::fabs(static_cast<float>(offset));
    const float falloff = 1.0f / (1.0f + std::max(distance - 1.0f, 0.0f));
    const float alpha = std::clamp(opacity, 0.0f, 1.0f) * falloff;
    return vec4(rgb, alpha);
}

}  // namespace ms
