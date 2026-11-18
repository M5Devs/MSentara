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

#include "TestCommon.hpp"

#include "core/Camera.hpp"

using namespace ms;
using namespace ms::test;

namespace {

void test_default_position() {
    Camera cam;
    cam.target = vec3(0.0f);
    cam.yaw = 0.0f;
    cam.pitch = 0.0f;
    cam.distance = 10.0f;
    // yaw=0, pitch=0 places the camera on +Z looking towards the origin.
    MS_CHECK_NEAR(cam.position(), vec3(0.0f, 0.0f, 10.0f));
}

void test_position_distance_is_constant() {
    Camera cam;
    cam.distance = 7.0f;
    for (float yaw : {-170.0f, -45.0f, 0.0f, 60.0f}) {
        for (float pitch : {-80.0f, -10.0f, 0.0f, 30.0f, 80.0f}) {
            cam.yaw = yaw;
            cam.pitch = pitch;
            MS_CHECK(nearly(glm::length(cam.position() - cam.target), 7.0f, 1e-3f));
        }
    }
}

void test_basis_is_orthonormal() {
    Camera cam;
    cam.yaw = 37.0f;
    cam.pitch = 22.0f;
    const vec3 f = cam.forward();
    const vec3 r = cam.right();
    const vec3 u = cam.up();
    MS_CHECK(nearly(glm::length(f), 1.0f, 1e-3f));
    MS_CHECK(nearly(glm::length(r), 1.0f, 1e-3f));
    MS_CHECK(nearly(glm::length(u), 1.0f, 1e-3f));
    MS_CHECK(nearly(glm::dot(f, r), 0.0f, 1e-3f));
    MS_CHECK(nearly(glm::dot(f, u), 0.0f, 1e-3f));
    MS_CHECK(nearly(glm::dot(r, u), 0.0f, 1e-3f));
}

void test_forward_points_at_target() {
    Camera cam;
    cam.target = vec3(3.0f, -2.0f, 1.0f);
    cam.distance = 5.0f;
    const vec3 to_target = glm::normalize(cam.target - cam.position());
    MS_CHECK_NEAR(cam.forward(), to_target);
}

void test_orbit_clamps_pitch() {
    Camera cam;
    cam.orbit(0.0f, 100000.0f);  // enormous downward drag
    MS_CHECK(cam.pitch <= Camera::kMaxPitch + 1e-4f);
    cam.orbit(0.0f, -100000.0f);
    MS_CHECK(cam.pitch >= Camera::kMinPitch - 1e-4f);
}

void test_orbit_changes_yaw_only_by_dx() {
    Camera cam;
    const float yaw0 = cam.yaw;
    const float pitch0 = cam.pitch;
    cam.orbit(50.0f, 0.0f, 0.3f);
    MS_CHECK(nearly(cam.pitch, pitch0));
    MS_CHECK(!nearly(cam.yaw, yaw0));
}

void test_zoom_clamps_distance() {
    Camera cam;
    cam.distance = 8.0f;
    for (int i = 0; i < 500; ++i) {
        cam.zoom(1.0f);
    }
    MS_CHECK(cam.distance >= Camera::kMinDistance - 1e-5f);
    for (int i = 0; i < 5000; ++i) {
        cam.zoom(-1.0f);
    }
    MS_CHECK(cam.distance <= Camera::kMaxDistance + 1e-3f);
}

void test_pan_moves_target_perpendicular_to_view() {
    Camera cam;
    cam.yaw = 0.0f;
    cam.pitch = 0.0f;
    cam.distance = 10.0f;
    const vec3 before = cam.target;
    cam.pan(20.0f, 0.0f, 600.0f);
    const vec3 delta = cam.target - before;
    // Horizontal pan must stay perpendicular to the view direction.
    MS_CHECK(nearly(glm::dot(delta, cam.forward()), 0.0f, 1e-3f));
    MS_CHECK(glm::length(delta) > 0.0f);
}

void test_frame_fits_radius() {
    Camera cam;
    cam.frame(vec3(1.0f, 2.0f, 3.0f), 2.0f);
    MS_CHECK_NEAR(cam.target, vec3(1.0f, 2.0f, 3.0f));
    // The object must fit inside the vertical FOV with a little margin.
    const float half_fov = glm::radians(cam.fov_y) * 0.5f;
    MS_CHECK(cam.distance * std::tan(half_fov) >= 2.0f);
}

void test_view_presets() {
    Camera cam;
    cam.set_view_front();
    MS_CHECK(nearly(cam.position().z, cam.distance, 1e-3f));
    MS_CHECK(nearly(cam.position().x, 0.0f, 1e-3f));

    cam.set_view_top();
    MS_CHECK(cam.position().y > 0.0f);
    MS_CHECK(nearly(cam.pitch, Camera::kMaxPitch));

    cam.set_view_right();
    MS_CHECK(nearly(cam.yaw, 90.0f));
}

void test_projection_matrices() {
    Camera cam;
    const mat4 persp = cam.projection_matrix(16.0f / 9.0f);
    // A right-handed perspective projection (glm's convention) places -1 in
    // row 3, column 2 — the w output that divides by -z.
    MS_CHECK(nearly(persp[2][3], -1.0f));

    cam.orthographic = true;
    const mat4 ortho = cam.projection_matrix(16.0f / 9.0f);
    MS_CHECK(nearly(ortho[2][3], 0.0f));

    // Degenerate aspect must not produce NaNs.
    const mat4 safe = cam.projection_matrix(0.0f);
    MS_CHECK(std::isfinite(safe[0][0]));
}

void test_view_projection_is_well_formed() {
    Camera cam;
    const mat4 vp = cam.view_projection(1.5f);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            MS_CHECK(std::isfinite(vp[c][r]));
        }
    }
}

// ---------------------------------------------------------------------------
// VMD look-at state round-tripping
// ---------------------------------------------------------------------------

void test_vmd_state_round_trips_orbit() {
    Camera cam;
    cam.target = vec3(1.0f, 2.0f, 3.0f);
    cam.yaw = 40.0f;
    cam.pitch = -15.0f;
    cam.roll = 0.0f;
    cam.distance = 12.0f;
    cam.fov_y = 42.0f;
    cam.orthographic = false;

    const CameraState state = cam.vmd_state();
    Camera restored;
    restored.apply_vmd_state(state);

    MS_CHECK_NEAR(restored.target, cam.target);
    MS_CHECK(nearly(restored.yaw, cam.yaw, 1e-3f));
    MS_CHECK(nearly(restored.pitch, cam.pitch, 1e-3f));
    MS_CHECK(nearly(restored.distance, cam.distance, 1e-3f));
    MS_CHECK(nearly(restored.fov_y, cam.fov_y, 1e-3f));
    MS_CHECK(restored.orthographic == cam.orthographic);
}

void test_vmd_state_yaw_pitch_roll_mapping() {
    // VMD orientation is Ry(y) * Rz(-z) * Rx(x): yaw = y, pitch = -x, roll = -z.
    CameraState state;
    state.rotate = vec3(glm::radians(10.0f), glm::radians(20.0f),
                        glm::radians(30.0f));
    Camera cam;
    cam.apply_vmd_state(state);
    MS_CHECK(nearly(cam.yaw, 20.0f, 1e-3f));
    MS_CHECK(nearly(cam.pitch, -10.0f, 1e-3f));
    MS_CHECK(nearly(cam.roll, -30.0f, 1e-3f));
}

void test_vmd_state_interest_and_projection() {
    CameraState state;
    state.interest = vec3(4.0f, 5.0f, 6.0f);
    state.distance = 25.0f;
    state.fov = 35.0f;
    state.perspective = false;
    Camera cam;
    cam.apply_vmd_state(state);
    MS_CHECK_NEAR(cam.target, vec3(4.0f, 5.0f, 6.0f));
    MS_CHECK(nearly(cam.distance, 25.0f, 1e-3f));
    MS_CHECK(nearly(cam.fov_y, 35.0f, 1e-3f));
    MS_CHECK(cam.orthographic);
}

void test_roll_tilts_the_basis() {
    Camera cam;
    cam.yaw = 0.0f;
    cam.pitch = 0.0f;
    cam.roll = 0.0f;
    const vec3 up_before = cam.up();
    const vec3 right_before = cam.right();
    const vec3 forward_before = cam.forward();
    cam.roll = 90.0f;
    // A 90-degree roll swaps the up and right axes (up to sign).
    MS_CHECK(nearly(glm::dot(cam.up(), up_before), 0.0f, 1e-3f));
    MS_CHECK(nearly(glm::dot(cam.right(), right_before), 0.0f, 1e-3f));
    // Rolling must not change where the camera looks.
    MS_CHECK_NEAR(cam.forward(), forward_before);
}

}  // namespace

int main() {
    test_default_position();
    test_position_distance_is_constant();
    test_basis_is_orthonormal();
    test_forward_points_at_target();
    test_orbit_clamps_pitch();
    test_orbit_changes_yaw_only_by_dx();
    test_zoom_clamps_distance();
    test_pan_moves_target_perpendicular_to_view();
    test_frame_fits_radius();
    test_view_presets();
    test_projection_matrices();
    test_view_projection_is_well_formed();
    test_vmd_state_round_trips_orbit();
    test_vmd_state_yaw_pitch_roll_mapping();
    test_vmd_state_interest_and_projection();
    test_roll_tilts_the_basis();
    return ms::test::summary("camera");
}
