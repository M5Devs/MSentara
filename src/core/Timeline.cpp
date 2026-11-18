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

#include "core/Timeline.hpp"

#include <algorithm>
#include <cmath>

namespace ms {

const char* channel_name(Channel c) {
    switch (c) {
        case Channel::PositionX: return "Position X";
        case Channel::PositionY: return "Position Y";
        case Channel::PositionZ: return "Position Z";
        case Channel::RotationX: return "Rotation X";
        case Channel::RotationY: return "Rotation Y";
        case Channel::RotationZ: return "Rotation Z";
        case Channel::ScaleX:    return "Scale X";
        case Channel::ScaleY:    return "Scale Y";
        case Channel::ScaleZ:    return "Scale Z";
    }
    return "Unknown";
}

void Curve::set_key(int frame, float value) {
    for (Keyframe& k : keys) {
        if (k.frame == frame) {
            k.value = value;
            return;
        }
        if (k.frame > frame) {
            break;
        }
    }
    keys.push_back(Keyframe{frame, value});
    std::sort(keys.begin(), keys.end(),
              [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
}

bool Curve::remove_key(int frame) {
    const auto it = std::find_if(keys.begin(), keys.end(),
                                 [frame](const Keyframe& k) { return k.frame == frame; });
    if (it == keys.end()) {
        return false;
    }
    keys.erase(it);
    return true;
}

float Curve::sample(float frame, float fallback) const {
    if (keys.empty()) {
        return fallback;
    }
    if (frame <= static_cast<float>(keys.front().frame)) {
        return keys.front().value;
    }
    if (frame >= static_cast<float>(keys.back().frame)) {
        return keys.back().value;
    }

    for (std::size_t i = 1; i < keys.size(); ++i) {
        const Keyframe& a = keys[i - 1];
        const Keyframe& b = keys[i];
        if (frame <= static_cast<float>(b.frame)) {
            if (interpolation == Interpolation::Constant) {
                return a.value;
            }
            const float span = static_cast<float>(b.frame - a.frame);
            const float t = (span > 0.0f) ? (frame - static_cast<float>(a.frame)) / span : 0.0f;
            // Bezier is stored as a smooth-step approximation for now; a real
            // handle-based curve is a later milestone.
            const float eased =
                (interpolation == Interpolation::Bezier)
                    ? t * t * (3.0f - 2.0f * t)
                    : t;
            return a.value + (b.value - a.value) * eased;
        }
    }
    return keys.back().value;
}

Curve& Action::curve(Channel c) {
    for (Curve& cv : curves) {
        if (cv.channel == c) {
            return cv;
        }
    }
    curves.push_back(Curve{c, Interpolation::Linear, {}});
    return curves.back();
}

const Curve* Action::find_curve(Channel c) const {
    for (const Curve& cv : curves) {
        if (cv.channel == c) {
            return &cv;
        }
    }
    return nullptr;
}

int Action::frame_range_start() const {
    int start = 0;
    bool found = false;
    for (const Curve& cv : curves) {
        for (const Keyframe& k : cv.keys) {
            if (!found || k.frame < start) {
                start = k.frame;
                found = true;
            }
        }
    }
    return start;
}

int Action::frame_range_end() const {
    int end = 0;
    bool found = false;
    for (const Curve& cv : curves) {
        for (const Keyframe& k : cv.keys) {
            if (!found || k.frame > end) {
                end = k.frame;
                found = true;
            }
        }
    }
    return end;
}

float Timeline::duration_seconds() const {
    if (fps <= 0.0f) {
        return 0.0f;
    }
    return static_cast<float>(duration_frames()) / fps;
}

void Timeline::set_frame(float f) {
    current_frame = glm::clamp(f, static_cast<float>(frame_start),
                               static_cast<float>(frame_end));
}

void Timeline::step(int delta) {
    set_frame(current_frame + static_cast<float>(delta));
}

float Timeline::advance(float dt) {
    if (!playing || fps <= 0.0f) {
        return current_frame;
    }
    float next = current_frame + dt * fps;
    const float end = static_cast<float>(frame_end);
    const float start = static_cast<float>(frame_start);
    if (next > end) {
        if (loop) {
            next = start + std::fmod(next - start, end - start + 1.0f);
        } else {
            // Clamp to the last frame and stop, so a one-shot playback holds
            // its final pose instead of jumping back to the start.
            next = end;
            playing = false;
        }
    }
    set_frame(next);
    return current_frame;
}

}  // namespace ms
