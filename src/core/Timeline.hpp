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

#include <cstdint>
#include <string>
#include <vector>

#include "core/Math.hpp"

namespace ms {

// Which numeric channel a keyframe drives.
enum class Channel : std::uint8_t {
    PositionX, PositionY, PositionZ,
    RotationX, RotationY, RotationZ,
    ScaleX, ScaleY, ScaleZ,
};

inline constexpr int kChannelCount = 9;

const char* channel_name(Channel c);

// A single keyed value. `frame` is an integer frame number (Blender-style);
// `value` is the channel value in its natural unit (world units or degrees).
struct Keyframe {
    int frame = 0;
    float value = 0.0f;
};

// Interpolation applied to the segment that *starts* at this keyframe.
enum class Interpolation : std::uint8_t { Constant, Linear, Bezier };

// One animation channel: an ordered list of keyframes plus its interpolation.
struct Curve {
    Channel channel = Channel::PositionX;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<Keyframe> keys;

    // Inserts or replaces the key at `frame`, keeping `keys` sorted by frame.
    void set_key(int frame, float value);

    bool remove_key(int frame);

    // Samples the curve at `frame`, returning `fallback` when empty.
    float sample(float frame, float fallback = 0.0f) const;

    bool empty() const { return keys.empty(); }
};

// The animation attached to one scene entity.
struct Action {
    std::string name;
    std::vector<Curve> curves;

    Curve& curve(Channel c);
    const Curve* find_curve(Channel c) const;
    bool has_channel(Channel c) const { return find_curve(c) != nullptr; }

    // Lowest and highest keyed frame across all channels (0,0 when empty).
    int frame_range_start() const;
    int frame_range_end() const;
};

// Playback state shared by the timeline UI and the animation evaluator.
struct Timeline {
    int frame_start = 1;
    int frame_end = 250;
    float fps = 24.0f;
    float current_frame = 1.0f;
    bool playing = false;

    int duration_frames() const { return frame_end - frame_start; }
    float duration_seconds() const;

    void set_frame(float f);
    void step(int delta);

    // Advances playback by `dt` seconds. Returns the new current frame.
    float advance(float dt);
};

}  // namespace ms
