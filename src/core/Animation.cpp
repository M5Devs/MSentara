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

// The span index for a frame strictly inside a non-empty key range: the index
// `i` with keys[i-1].frame <= frame < keys[i].frame, so i is in [1, size-1].
// The caller has already handled the hold-before/after cases.
template <typename Key>
std::size_t span_index(const std::vector<Key>& keys, float frame) {
    std::size_t lo = 1;
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
// 2D layer tracks
// ---------------------------------------------------------------------------

void Vec2Track::sort_keys() {
    std::sort(keys.begin(), keys.end(),
              [](const Vec2Key& a, const Vec2Key& b) { return a.frame < b.frame; });
}

void Vec2Track::set_key(int frame, const vec2& value, const Bezier& bezier) {
    for (Vec2Key& k : keys) {
        if (k.frame == frame) {
            k.value = value;
            k.bezier = bezier;
            return;
        }
    }
    keys.push_back(Vec2Key{frame, value, bezier});
    sort_keys();
}

bool Vec2Track::remove_key(int frame) {
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].frame == frame) {
            keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

bool Vec2Track::has_key(int frame) const {
    for (const Vec2Key& k : keys) {
        if (k.frame == frame) {
            return true;
        }
    }
    return false;
}

vec2 Vec2Track::sample(float frame, const vec2& fallback) const {
    if (keys.empty()) {
        return fallback;
    }
    if (frame <= static_cast<float>(keys.front().frame)) {
        return keys.front().value;
    }
    if (frame >= static_cast<float>(keys.back().frame)) {
        return keys.back().value;
    }
    const std::size_t right = span_index(keys, frame);
    const Vec2Key& k0 = keys[right - 1];
    const Vec2Key& k1 = keys[right];
    const float span = static_cast<float>(k1.frame - k0.frame);
    const float time = (span > 0.0f)
                           ? (frame - static_cast<float>(k0.frame)) / span
                           : 0.0f;
    const float e = k0.bezier.ease(time);
    return glm::mix(k0.value, k1.value, e);
}

int Vec2Track::frame_start() const {
    return keys.empty() ? 0 : keys.front().frame;
}

int Vec2Track::frame_end() const {
    return keys.empty() ? 0 : keys.back().frame;
}

void FloatTrack::sort_keys() {
    std::sort(keys.begin(), keys.end(),
              [](const FloatKey& a, const FloatKey& b) { return a.frame < b.frame; });
}

void FloatTrack::set_key(int frame, float value, const Bezier& bezier) {
    for (FloatKey& k : keys) {
        if (k.frame == frame) {
            k.value = value;
            k.bezier = bezier;
            return;
        }
    }
    keys.push_back(FloatKey{frame, value, bezier});
    sort_keys();
}

bool FloatTrack::remove_key(int frame) {
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].frame == frame) {
            keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

bool FloatTrack::has_key(int frame) const {
    for (const FloatKey& k : keys) {
        if (k.frame == frame) {
            return true;
        }
    }
    return false;
}

float FloatTrack::sample(float frame, float fallback) const {
    if (keys.empty()) {
        return fallback;
    }
    if (frame <= static_cast<float>(keys.front().frame)) {
        return keys.front().value;
    }
    if (frame >= static_cast<float>(keys.back().frame)) {
        return keys.back().value;
    }
    const std::size_t right = span_index(keys, frame);
    const FloatKey& k0 = keys[right - 1];
    const FloatKey& k1 = keys[right];
    const float span = static_cast<float>(k1.frame - k0.frame);
    const float time = (span > 0.0f)
                           ? (frame - static_cast<float>(k0.frame)) / span
                           : 0.0f;
    const float e = k0.bezier.ease(time);
    return k0.value + (k1.value - k0.value) * e;
}

int FloatTrack::frame_start() const {
    return keys.empty() ? 0 : keys.front().frame;
}

int FloatTrack::frame_end() const {
    return keys.empty() ? 0 : keys.back().frame;
}

Layer2DTrackState Layer2DTrack::sample(float frame,
                                       const Layer2DTrackState& base) const {
    // A channel with no keys leaves its field at the base value, so a track
    // that only animates opacity never resets the layer's position.
    Layer2DTrackState out = base;
    if (!position.empty()) {
        out.position = position.sample(frame, base.position);
    }
    if (!scale.empty()) {
        out.scale = scale.sample(frame, base.scale);
    }
    if (!rotation.empty()) {
        out.rotation = rotation.sample(frame, base.rotation);
    }
    if (!opacity.empty()) {
        out.opacity = opacity.sample(frame, base.opacity);
    }
    return out;
}

int Layer2DTrack::frame_start() const {
    int start = 0;
    bool any = false;
    const auto consider = [&](int f) {
        if (!any || f < start) {
            start = f;
        }
        any = true;
    };
    if (!position.empty()) consider(position.frame_start());
    if (!scale.empty()) consider(scale.frame_start());
    if (!rotation.empty()) consider(rotation.frame_start());
    if (!opacity.empty()) consider(opacity.frame_start());
    return any ? start : 0;
}

int Layer2DTrack::frame_end() const {
    int end = 0;
    end = std::max(end, position.frame_end());
    end = std::max(end, scale.frame_end());
    end = std::max(end, rotation.frame_end());
    end = std::max(end, opacity.frame_end());
    return end;
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

const Layer2DTrack* AnimationClip::find_layer_2d(
    const std::string& layer_name) const {
    for (const Layer2DTrack& t : layer_2d_tracks) {
        if (t.name == layer_name) {
            return &t;
        }
    }
    return nullptr;
}

Layer2DTrack& AnimationClip::layer_2d(const std::string& layer_name) {
    for (Layer2DTrack& t : layer_2d_tracks) {
        if (t.name == layer_name) {
            return t;
        }
    }
    layer_2d_tracks.push_back(Layer2DTrack{});
    layer_2d_tracks.back().name = layer_name;
    return layer_2d_tracks.back();
}

int AnimationClip::frame_end() const {
    int end = 0;
    for (const BoneTrack& t : bone_tracks) {
        end = std::max(end, t.frame_end());
    }
    for (const ScalarTrack& t : scalar_tracks) {
        end = std::max(end, t.frame_end());
    }
    for (const Layer2DTrack& t : layer_2d_tracks) {
        end = std::max(end, t.frame_end());
    }
    end = std::max(end, camera_track.frame_end());
    return end;
}

// ---------------------------------------------------------------------------
// CameraTrack
// ---------------------------------------------------------------------------

void CameraTrack::sort_keys() {
    std::sort(keys.begin(), keys.end(),
              [](const CameraKey& a, const CameraKey& b) {
                  return a.frame < b.frame;
              });
}

int CameraTrack::frame_start() const {
    return keys.empty() ? 0 : keys.front().frame;
}

int CameraTrack::frame_end() const {
    return keys.empty() ? 0 : keys.back().frame;
}

CameraState CameraTrack::sample(float frame) const {
    if (keys.empty()) {
        return CameraState{};
    }
    if (frame <= static_cast<float>(keys.front().frame)) {
        return keys.front().state;
    }
    if (frame >= static_cast<float>(keys.back().frame)) {
        return keys.back().state;
    }

    // Find the span: k0 is the key at or before `frame`, k1 the next key.
    const std::size_t right = static_cast<std::size_t>(
        std::upper_bound(keys.begin(), keys.end(), frame,
                         [](float f, const CameraKey& k) {
                             return f < static_cast<float>(k.frame);
                         }) -
        keys.begin());
    const CameraKey& k0 = keys[right - 1];
    const CameraKey& k1 = keys[right];

    // Two keys one frame apart are a hard cut, not a one-frame interpolation:
    // this is how MMD marks a camera switch, and the reference implementation
    // holds the earlier key across that span.
    if (k1.frame - k0.frame <= 1) {
        return k0.state;
    }

    const float span = static_cast<float>(k1.frame - k0.frame);
    const float time = (frame - static_cast<float>(k0.frame)) / span;

    // Each of the six curves eases independently, exactly as the reference
    // implementation does.
    const float ex = k0.bezier_x.ease(time);
    const float ey = k0.bezier_y.ease(time);
    const float ez = k0.bezier_z.ease(time);
    const float erot = k0.bezier_rot.ease(time);
    const float edist = k0.bezier_distance.ease(time);
    const float efov = k0.bezier_fov.ease(time);

    CameraState out;
    out.interest = glm::mix(k0.state.interest, k1.state.interest,
                            vec3(ex, ey, ez));
    out.rotate = glm::mix(k0.state.rotate, k1.state.rotate, erot);
    out.distance = glm::mix(k0.state.distance, k1.state.distance, edist);
    out.fov = glm::mix(k0.state.fov, k1.state.fov, efov);
    // The projection mode is discrete; hold the earlier key's, matching how a
    // step (constant) interpolation behaves.
    out.perspective = k0.state.perspective;
    return out;
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

bool apply_camera_clip(const AnimationClip& clip, float frame, Camera& camera) {
    if (clip.camera_track.empty()) {
        return false;
    }
    camera.apply_vmd_state(clip.camera_track.sample(frame));
    return true;
}

bool apply_layer_2d_clip(const AnimationClip& clip, float frame, Scene2D& scene) {
    bool applied = false;
    for (const Layer2DTrack& track : clip.layer_2d_tracks) {
        if (track.empty()) {
            continue;
        }
        for (Layer2D& layer : scene.layers()) {
            if (layer.name != track.name) {
                continue;
            }
            // The layer's authored values are the base; a channel with no keys
            // keeps its base value, so a track only overrides what it keys.
            Layer2DTrackState base;
            base.position = layer.position;
            base.rotation = layer.rotation;
            base.scale = layer.scale;
            base.opacity = layer.opacity;

            const Layer2DTrackState sampled = track.sample(frame, base);
            layer.position = sampled.position;
            layer.rotation = sampled.rotation;
            layer.scale = sampled.scale;
            layer.opacity = sampled.opacity;
            applied = true;
            break;
        }
    }
    return applied;
}

}  // namespace ms
