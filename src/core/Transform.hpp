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

// Position / Euler-rotation (degrees) / Scale, matching what a Blender-style
// property panel edits. Kept as plain data so it is trivially serialisable
// later by the (future) format parsers.
struct Transform {
    vec3 position{0.0f, 0.0f, 0.0f};
    vec3 rotation{0.0f, 0.0f, 0.0f};  // Euler XYZ, degrees
    vec3 scale{1.0f, 1.0f, 1.0f};

    // Composed local matrix: T * R * S (rotation applied X, then Y, then Z).
    mat4 matrix() const;

    // Rebuild a TRS transform from a 4x4 matrix, assuming the matrix is a
    // pure TRS composition (no shear). Used to read back edits made by the
    // ImGuizmo manipulator.
    static Transform from_matrix(const mat4& m);
};

}  // namespace ms
