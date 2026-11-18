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

vec3 spherical_offset(float yaw_deg, float pitch_deg, float distance) {
    const float y = yaw_deg * kDegToRad;
    const float p = pitch_deg * kDegToRad;
    const float cp = std::cos(p);
    return vec3(distance * cp * std::sin(y), distance * std::sin(p),
                distance * cp * std::cos(y));
}
}  // namespace

vec3 Camera::position() const {
    return target + spherical_offset(yaw, pitch, distance);
}

vec3 Camera::forward() const { return glm::normalize(target - position()); }

vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), vec3(0.0f, 1.0f, 0.0f)));
}

vec3 Camera::up() const { return glm::normalize(glm::cross(right(), forward())); }

mat4 Camera::view_matrix() const {
    return glm::lookAt(position(), target, vec3(0.0f, 1.0f, 0.0f));
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
