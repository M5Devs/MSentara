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
#include "core/Skeleton.hpp"
#include "core/Transform.hpp"

namespace ms {

// ---------------------------------------------------------------------------
// Bezier interpolation
// ---------------------------------------------------------------------------

// MMD's interpolation curves are cubic Beziers with the first control point at
// the origin and the last at (1, 1), so only two interior control points are
// stored. Each point is normalised: the on-disk byte is divided by 127, which
// is exactly what the reference implementation (saba's VMDAnimation.cpp) does.
//
// The curve is parametric in `t`. To find the eased value for a given time we
// must first solve x(t) = time (there is no closed form), then evaluate y at
// that t. That is what sample() does, via a bisection that mirrors saba's
// FindBezierX.
struct Bezier {
    vec2 cp1{0.0f, 0.0f};  // first interior control point
    vec2 cp2{1.0f, 1.0f};  // second interior control point

    // The two points a linear curve degenerates to, i.e. y == x.
    static Bezier linear();

    // Evaluates the cubic at the curve parameter `t` in [0, 1].
    float eval_x(float t) const;
    float eval_y(float t) const;
    vec2 eval(float t) const;

    // Solves x(t) = time for t by bisection (there is no closed form), then
    // returns y(t): the eased fraction for a keyframe span at normalised
    // `time` in [0, 1].
    float ease(float time) const;
};

// ---------------------------------------------------------------------------
// Tracks
// ---------------------------------------------------------------------------

// One bone's keyframes. Translation and rotation are keyed together on the
// same frames (MMD stores them in a single record), and each key carries four
// independent Bezier curves: one per translation axis and one for rotation.
struct BoneKey {
    int frame = 0;
    vec3 translation{0.0f, 0.0f, 0.0f};
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    // Interpolation applied over the span that *starts* at this key.
    Bezier bezier_x;
    Bezier bezier_y;
    Bezier bezier_z;
    Bezier bezier_rot;
};

// A named channel of animation. The name is a bone name for skeletal tracks;
// a future 2D/property track uses the same structure with a property name, so
// the evaluator stays generic.
struct BoneTrack {
    std::string name;
    std::vector<BoneKey> keys;

    // Keeps `keys` ordered by frame.
    void sort_keys();

    // Sampled translation and rotation at `frame`. Outside the keyed range the
    // nearest key is held (constant extrapolation), matching MMD.
    void sample(float frame, vec3& translation, quat& rotation) const;

    bool empty() const { return keys.empty(); }
    int frame_start() const;
    int frame_end() const;
};

// A generic keyed scalar track, for morphs, camera properties, or any future
// 2D/property channel. Linear between keys, held outside the range.
struct ScalarKey {
    int frame = 0;
    float value = 0.0f;
};

struct ScalarTrack {
    std::string name;
    std::vector<ScalarKey> keys;

    void sort_keys();
    float sample(float frame, float fallback = 0.0f) const;

    bool empty() const { return keys.empty(); }
    int frame_start() const;
    int frame_end() const;
};

// ---------------------------------------------------------------------------
// Clip
// ---------------------------------------------------------------------------

// A complete animation: one or more bone tracks plus any number of generic
// scalar tracks. Nothing here is skeletal-specific beyond the bone tracks, so
// the same clip type can drive 2D rigs or non-skeletal properties later.
struct AnimationClip {
    std::string name;
    float fps = 30.0f;  // MMD's fixed timing
    std::vector<BoneTrack> bone_tracks;
    std::vector<ScalarTrack> scalar_tracks;

    const BoneTrack* find_bone(const std::string& bone_name) const;
    BoneTrack& bone(const std::string& bone_name);
    const ScalarTrack* find_scalar(const std::string& track_name) const;
    ScalarTrack& scalar(const std::string& track_name);

    // Highest keyed frame across every track, or 0 when empty. Drives the
    // timeline's end frame after an import.
    int frame_end() const;

    bool empty() const {
        return bone_tracks.empty() && scalar_tracks.empty();
    }
};

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

// Applies `clip` at time `frame` onto `pose` for the given `skeleton`.
//
// Track bone names are matched to skeleton bone names; unmatched tracks are
// ignored, so a clip authored for one model can be played on another that
// shares bone names. Each sampled transform replaces that bone's delta (a bone
// with no track keeps its rest delta). Call evaluate_pose() afterwards to
// recompute local/global/palette.
void apply_clip(const Skeleton& skeleton, const AnimationClip& clip,
                float frame, Pose& pose);

}  // namespace ms
