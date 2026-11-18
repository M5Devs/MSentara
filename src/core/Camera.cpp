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

#include "core/Camera.hpp"

#include <algorithm>
#include <cmath>

namespace ms {

namespace {
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

vec3 spherical_offset(float yaw_deg, float pitch_deg, float distance) {
    const float y = yaw_deg * kDegToRad;
    const float p = pitch_deg * kDegToRad;
    const float cp = std::cos(p);
    return vec3(distance * cp * std::sin(y), distance * std::sin(p),
                distance * cp * std::cos(y));
}

// Rotates `v` about the unit axis `axis` by `angle` radians (Rodrigues).
vec3 rotate_about(const vec3& v, const vec3& axis, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return v * c + glm::cross(axis, v) * s + axis * glm::dot(axis, v) * (1.0f - c);
}

// The un-rolled basis for the current orbit angles: forward, right and up.
void orbit_basis(float yaw, float pitch, vec3& forward, vec3& right, vec3& up) {
    const float y = yaw * kDegToRad;
    const float p = pitch * kDegToRad;
    const float cp = std::cos(p);
    // The camera sits at +offset and looks back towards the target, so the
    // forward axis is the negated, normalised offset.
    forward = vec3(-cp * std::sin(y), -std::sin(p), -cp * std::cos(y));
    right = glm::normalize(glm::cross(forward, vec3(0.0f, 1.0f, 0.0f)));
    up = glm::cross(right, forward);
}
}  // namespace

vec3 Camera::position() const {
    return target + spherical_offset(yaw, pitch, distance);
}

vec3 Camera::forward() const {
    vec3 f, r, u;
    orbit_basis(yaw, pitch, f, r, u);
    return f;
}

vec3 Camera::right() const {
    vec3 f, r, u;
    orbit_basis(yaw, pitch, f, r, u);
    return (roll != 0.0f) ? rotate_about(r, f, roll * kDegToRad) : r;
}

vec3 Camera::up() const {
    vec3 f, r, u;
    orbit_basis(yaw, pitch, f, r, u);
    return (roll != 0.0f) ? rotate_about(u, f, roll * kDegToRad) : u;
}

mat4 Camera::view_matrix() const {
    return glm::lookAt(position(), target, up());
}

mat4 Camera::projection_matrix(float aspect) const {
    if (aspect <= 0.0f) {
        aspect = 1.0f;
    }
    if (orthographic) {
        // Match the perspective framing at the target distance.
        const float half_h = distance * std::tan(fov_y * 0.5f * kDegToRad);
        const float half_w = half_h * aspect;
        return glm::ortho(-half_w, half_w, -half_h, half_h, -far_plane, far_plane);
    }
    return glm::perspective(fov_y * kDegToRad, aspect, near_plane, far_plane);
}

mat4 Camera::view_projection(float aspect) const {
    return projection_matrix(aspect) * view_matrix();
}

mat4 Camera::projection_matrix_2d(float aspect) const {
    if (aspect <= 0.0f) {
        aspect = 1.0f;
    }
    // Half-height in pixels is the 2D zoom; half-width follows the aspect.
    const float half_h = std::max(distance, 0.001f);
    const float half_w = half_h * aspect;
    // y points down in pixel space, so the top/bottom arguments are swapped.
    // The view matrix is identity in 2D mode, so target.xy is the view centre.
    return glm::ortho(target.x - half_w, target.x + half_w,
                      target.y + half_h, target.y - half_h,
                      -1.0f, 1.0f);
}

void Camera::orbit(float dx, float dy, float sensitivity) {
    yaw -= dx * sensitivity;
    pitch += dy * sensitivity;
    pitch = glm::clamp(pitch, kMinPitch, kMaxPitch);
    if (yaw > 360.0f) yaw -= 360.0f;
    if (yaw < -360.0f) yaw += 360.0f;
}

void Camera::pan(float dx, float dy, float viewport_height) {
    if (viewport_height <= 0.0f) {
        return;
    }
    // World units per pixel at the target plane.
    const float world_per_px =
        2.0f * distance * std::tan(fov_y * 0.5f * kDegToRad) / viewport_height;
    target += (-right() * dx + up() * dy) * world_per_px;
}

void Camera::zoom(float steps, float sensitivity) {
    const float factor = std::pow(1.0f - sensitivity, steps);
    distance = glm::clamp(distance * factor, kMinDistance, kMaxDistance);
}

void Camera::frame(const vec3& center, float radius) {
    target = center;
    const float safe_radius = std::max(radius, 0.001f);
    distance = safe_radius / std::tan(fov_y * 0.5f * kDegToRad) * 1.2f;
    distance = glm::clamp(distance, kMinDistance, kMaxDistance);
}

// VMD's camera orientation is Ry(y) * Rz(-z) * Rx(x). With no roll (z == 0)
// that is exactly the orbit convention used everywhere else in the app, with
// yaw = y and pitch = -x, so a VMD shot maps onto the same yaw/pitch fields the
// viewport edits. A non-zero roll (-z) is applied on top by right()/up(), which
// reproduces the shot's tilt without introducing a second camera
// representation to keep in sync.
void Camera::apply_vmd_state(const CameraState& state) {
    target = state.interest;
    distance = glm::clamp(state.distance, kMinDistance, kMaxDistance);
    yaw = state.rotate.y * kRadToDeg;
    pitch = glm::clamp(-state.rotate.x * kRadToDeg, kMinPitch, kMaxPitch);
    roll = -state.rotate.z * kRadToDeg;
    fov_y = state.fov;
    orthographic = !state.perspective;
}

CameraState Camera::vmd_state() const {
    CameraState state;
    state.interest = target;
    state.distance = distance;
    state.rotate = vec3(-pitch * kDegToRad, yaw * kDegToRad, -roll * kDegToRad);
    state.fov = fov_y;
    state.perspective = !orthographic;
    return state;
}

void Camera::set_view_front() { yaw = 0.0f; pitch = 0.0f; }
void Camera::set_view_back() { yaw = 180.0f; pitch = 0.0f; }
void Camera::set_view_right() { yaw = 90.0f; pitch = 0.0f; }
void Camera::set_view_left() { yaw = -90.0f; pitch = 0.0f; }
void Camera::set_view_top() { yaw = 0.0f; pitch = kMaxPitch; }
void Camera::set_view_bottom() { yaw = 0.0f; pitch = kMinPitch; }
void Camera::set_view_camera_persp() {
    yaw = -35.0f;
    pitch = 25.0f;
    orthographic = false;
}

}  // namespace ms
