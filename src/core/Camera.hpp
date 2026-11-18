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

#include "core/Math.hpp"

namespace ms {

// ---------------------------------------------------------------------------
// Look-at camera state (what a VMD camera keyframe describes)
// ---------------------------------------------------------------------------

// The parameters a VMD camera keyframe carries, in MSentara's right-handed
// space (the VMD loader mirrors MMD's left-handed values before they land
// here).
//
// MMD's camera is a "look-at" rig rather than an orbit rig: `interest` is the
// point it looks at, `rotate` is its orientation as Euler XYZ radians,
// `distance` is how far the eye sits behind the interest along the view
// direction, `fov` is the vertical field of view in degrees, and `perspective`
// selects a perspective or orthographic projection.
struct CameraState {
    vec3 interest{0.0f, 10.0f, 0.0f};
    vec3 rotate{0.0f, 0.0f, 0.0f};  // Euler XYZ, radians
    float distance = 50.0f;
    float fov = 30.0f;  // vertical field of view, degrees
    bool perspective = true;
};

// Blender-style orbit camera.
//
// The camera always looks at `target`, positioned on a sphere described by
// `distance`, `yaw` and `pitch`. This makes orbiting, panning and zooming
// numerically stable (no accumulated drift) and trivially undoable.
//
// Blender default bindings, as used by the UI layer:
//   - middle-drag              : orbit
//   - shift + middle-drag      : pan
//   - ctrl + middle-drag, wheel: zoom (dolly)
class Camera {
public:
    vec3 target{0.0f, 0.0f, 0.0f};
    float distance = 8.0f;
    float yaw = -35.0f;    // degrees around world Y
    float pitch = 25.0f;   // degrees above the XZ plane, clamped to +-89
    float fov_y = 45.0f;   // vertical field of view, degrees
    float roll = 0.0f;     // camera roll, degrees (VMD camera tracks use it)
    float near_plane = 0.05f;
    float far_plane = 500.0f;
    bool orthographic = false;

    static constexpr float kMinPitch = -89.0f;
    static constexpr float kMaxPitch = 89.0f;
    static constexpr float kMinDistance = 0.05f;
    static constexpr float kMaxDistance = 400.0f;

    vec3 position() const;
    vec3 forward() const;
    vec3 right() const;
    vec3 up() const;

    mat4 view_matrix() const;
    mat4 projection_matrix(float aspect) const;
    mat4 view_projection(float aspect) const;

    // Rotates the camera around the target. `dx`/`dy` are in pixels.
    void orbit(float dx, float dy, float sensitivity = 0.3f);

    // Slides the target along the camera's right/up axes, scaled by distance
    // so the cursor keeps tracking the same world point regardless of zoom.
    void pan(float dx, float dy, float viewport_height);

    // Dollies towards/away from the target. Positive `steps` moves closer.
    void zoom(float steps, float sensitivity = 0.1f);

    // Frames a sphere: keeps the current orientation, fits the radius.
    void frame(const vec3& center, float radius);

    // Applies a VMD look-at camera state (from CameraState, i.e. an evaluated
    // camera keyframe). The camera keeps the orbit representation the rest of
    // the app uses: `target` becomes the interest point, and yaw/pitch are
    // recovered from the state's Euler rotation so a VMD shot is reproducible
    // from the same fields the UI edits. `roll` and the projection mode are
    // taken from the state directly.
    void apply_vmd_state(const CameraState& state);

    // Builds a state from the current orbit parameters, for round-tripping and
    // for tests. The inverse of apply_vmd_state.
    CameraState vmd_state() const;

    // Standard Blender view presets (numpad 1/3/7 etc.).
    void set_view_front();
    void set_view_back();
    void set_view_right();
    void set_view_left();
    void set_view_top();
    void set_view_bottom();
    void set_view_camera_persp();
};

}  // namespace ms
