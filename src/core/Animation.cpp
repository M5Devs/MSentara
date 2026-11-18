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

#include "core/Animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace ms {

namespace {

// Index of the first key with frame > `frame`, i.e. the right end of the span
// containing `frame`. Returns keys.size() when `frame` is at or past the last
// key. Keys must be sorted.
std::size_t upper_bound_key(const std::vector<BoneKey>& keys, float frame) {
    std::size_t lo = 0;
    std::size_t hi = keys.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (static_cast<float>(keys[mid].frame) <= frame) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

}  // namespace

// ---------------------------------------------------------------------------
// Bezier
// ---------------------------------------------------------------------------

Bezier Bezier::linear() {
    // Control points at 1/3 and 2/3 make the cubic collapse to y == x.
    Bezier b;
    b.cp1 = vec2(1.0f / 3.0f, 1.0f / 3.0f);
    b.cp2 = vec2(2.0f / 3.0f, 2.0f / 3.0f);
    return b;
}

float Bezier::eval_x(float t) const {
    const float it = 1.0f - t;
    // Cubic Bezier with p0 = (0,0) and p3 = (1,1):
    //   x(t) = 3 t (1-t)^2 cp1.x + 3 t^2 (1-t) cp2.x + t^3
    return 3.0f * t * it * it * cp1.x + 3.0f * t * t * it * cp2.x + t * t * t;
}

float Bezier::eval_y(float t) const {
    const float it = 1.0f - t;
    return 3.0f * t * it * it * cp1.y + 3.0f * t * t * it * cp2.y + t * t * t;
}

vec2 Bezier::eval(float t) const {
    return vec2(eval_x(t), eval_y(t));
}

float Bezier::ease(float time) const {
    if (time <= 0.0f) {
        return 0.0f;
    }
    if (time >= 1.0f) {
        return 1.0f;
    }

    // Bisection on t to solve x(t) = time. The same 1e-5 tolerance the
    // reference implementation uses; 24 iterations always converge well past
    // float precision.
    float lo = 0.0f;
    float hi = 1.0f;
    float t = 0.5f;
    for (int i = 0; i < 24; ++i) {
        const float x = eval_x(t);
        if (std::fabs(time - x) <= 1.0e-5f) {
            break;
        }
        if (time < x) {
            hi = t;
        } else {
            lo = t;
        }
        t = 0.5f * (lo + hi);
    }
    return eval_y(t);
}

// ---------------------------------------------------------------------------
// BoneTrack
// ---------------------------------------------------------------------------

void BoneTrack::sort_keys() {
    std::sort(keys.begin(), keys.end(),
              [](const BoneKey& a, const BoneKey& b) {
                  return a.frame < b.frame;
              });
}

int BoneTrack::frame_start() const {
    return keys.empty() ? 0 : keys.front().frame;
}

int BoneTrack::frame_end() const {
    return keys.empty() ? 0 : keys.back().frame;
}

void BoneTrack::sample(float frame, vec3& translation, quat& rotation) const {
    if (keys.empty()) {
        translation = vec3(0.0f);
        rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    if (frame <= static_cast<float>(keys.front().frame)) {
        translation = keys.front().translation;
        rotation = keys.front().rotation;
        return;
    }
    if (frame >= static_cast<float>(keys.back().frame)) {
        translation = keys.back().translation;
        rotation = keys.back().rotation;
        return;
    }

    const std::size_t right = upper_bound_key(keys, frame);
    // The range guards above ensure right is in [1, keys.size()-1].
    const BoneKey& k0 = keys[right - 1];
    const BoneKey& k1 = keys[right];

    const float span = static_cast<float>(k1.frame - k0.frame);
    const float time = (span > 0.0f)
                           ? (frame - static_cast<float>(k0.frame)) / span
                           : 0.0f;

    // Each axis eases on its own curve; rotation uses a single curve.
    const float ex = k0.bezier_x.ease(time);
    const float ey = k0.bezier_y.ease(time);
    const float ez = k0.bezier_z.ease(time);
    const float er = k0.bezier_rot.ease(time);

    translation = glm::mix(k0.translation, k1.translation, vec3(ex, ey, ez));
    rotation = glm::slerp(k0.rotation, k1.rotation, er);
}

// ---------------------------------------------------------------------------
// ScalarTrack
// ---------------------------------------------------------------------------

void ScalarTrack::sort_keys() {
    std::sort(keys.begin(), keys.end(),
              [](const ScalarKey& a, const ScalarKey& b) {
                  return a.frame < b.frame;
              });
}

int ScalarTrack::frame_start() const {
    return keys.empty() ? 0 : keys.front().frame;
}

int ScalarTrack::frame_end() const {
    return keys.empty() ? 0 : keys.back().frame;
}

float ScalarTrack::sample(float frame, float fallback) const {
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
        if (frame <= static_cast<float>(keys[i].frame)) {
            const ScalarKey& a = keys[i - 1];
            const ScalarKey& b = keys[i];
            const float span = static_cast<float>(b.frame - a.frame);
            const float t = (span > 0.0f)
                                ? (frame - static_cast<float>(a.frame)) / span
                                : 0.0f;
            return a.value + (b.value - a.value) * t;
        }
    }
    return keys.back().value;
}

// ---------------------------------------------------------------------------
// AnimationClip
// ---------------------------------------------------------------------------

const BoneTrack* AnimationClip::find_bone(const std::string& bone_name) const {
    for (const BoneTrack& t : bone_tracks) {
        if (t.name == bone_name) {
            return &t;
        }
    }
    return nullptr;
}

BoneTrack& AnimationClip::bone(const std::string& bone_name) {
    for (BoneTrack& t : bone_tracks) {
        if (t.name == bone_name) {
            return t;
        }
    }
    bone_tracks.push_back(BoneTrack{});
    bone_tracks.back().name = bone_name;
    return bone_tracks.back();
}

const ScalarTrack* AnimationClip::find_scalar(
    const std::string& track_name) const {
    for (const ScalarTrack& t : scalar_tracks) {
        if (t.name == track_name) {
            return &t;
        }
    }
    return nullptr;
}

ScalarTrack& AnimationClip::scalar(const std::string& track_name) {
    for (ScalarTrack& t : scalar_tracks) {
        if (t.name == track_name) {
            return t;
        }
    }
    scalar_tracks.push_back(ScalarTrack{});
    scalar_tracks.back().name = track_name;
    return scalar_tracks.back();
}

int AnimationClip::frame_end() const {
    int end = 0;
    for (const BoneTrack& t : bone_tracks) {
        end = std::max(end, t.frame_end());
    }
    for (const ScalarTrack& t : scalar_tracks) {
        end = std::max(end, t.frame_end());
    }
    return end;
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

void apply_clip(const Skeleton& skeleton, const AnimationClip& clip,
                float frame, Pose& pose) {
    if (pose.size() != skeleton.size()) {
        pose.resize(skeleton.size());
    }
    const std::size_t bone_count = skeleton.size();

    for (const BoneTrack& track : clip.bone_tracks) {
        if (track.empty()) {
            continue;
        }
        // Map the track to a skeleton bone by name. A clip may name bones the
        // rig does not have (or vice versa); those are simply skipped.
        for (std::size_t b = 0; b < bone_count; ++b) {
            if (skeleton.bones()[b].name != track.name) {
                continue;
            }

            vec3 translation;
            quat rotation;
            track.sample(frame, translation, rotation);

            Transform sampled;
            sampled.position = translation;
            // The rig's delta is a TRS whose rotation is Euler XYZ; convert
            // the sampled quaternion into those angles so it composes with the
            // rest transform the same way a gizmo edit does.
            sampled.rotation = quat_to_euler_degrees(rotation);

            pose.delta[b] = sampled;
            break;
        }
    }
}

}  // namespace ms
