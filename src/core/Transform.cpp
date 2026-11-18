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

#include "core/Transform.hpp"

#include <cmath>

namespace ms {

// Rotation order is R = Rx * Ry * Rz (the order ImGuizmo's
// RecomposeMatrixFromComponents uses), so a matrix produced by the manipulator
// decomposes back to the same Euler angles the property panel shows.
//
// With c* = cos(angle), s* = sin(angle), the composed rotation is:
//   R[0][0] = cy*cz           R[0][1] = -cy*sz          R[0][2] = sy
//   R[1][0] = cx*sz + sx*sy*cz  R[1][1] = cx*cz - sx*sy*sz  R[1][2] = -sx*cy
//   R[2][0] = sx*sz - cx*sy*cz  R[2][1] = sx*cz + cx*sy*sz  R[2][2] = cx*cy
// glm stores column-major, so R[row][col] is m[col][row].

mat4 Transform::matrix() const {
    mat4 m = glm::translate(mat4(1.0f), position);
    // Post-multiplying in X, Y, Z order yields R = Rx * Ry * Rz.
    m = glm::rotate(m, glm::radians(rotation.x), vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, glm::radians(rotation.y), vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, glm::radians(rotation.z), vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(m, scale);
}

Transform Transform::from_matrix(const mat4& m) {
    Transform t;
    t.position = vec3(m[3]);

    vec3 c0(m[0]);
    vec3 c1(m[1]);
    vec3 c2(m[2]);
    t.scale = vec3(glm::length(c0), glm::length(c1), glm::length(c2));

    // Strip the scale so the remaining basis is a pure rotation.
    if (t.scale.x != 0.0f) c0 /= t.scale.x;
    if (t.scale.y != 0.0f) c1 /= t.scale.y;
    if (t.scale.z != 0.0f) c2 /= t.scale.z;

    // Column c0 = (R[0][0], R[1][0], R[2][0]), and so on. Reading the
    // components the derivation above needs:
    //   R[0][2] = c2.x, R[1][2] = c2.y, R[2][2] = c2.z
    //   R[0][1] = c1.x, R[0][0] = c0.x
    //   R[1][0] = c0.y, R[1][1] = c1.y
    const float sy = glm::clamp(c2.x, -1.0f, 1.0f);
    t.rotation.y = glm::degrees(std::asin(sy));
    if (std::abs(sy) < 0.99999f) {
        t.rotation.x = glm::degrees(std::atan2(-c2.y, c2.z));
        t.rotation.z = glm::degrees(std::atan2(-c1.x, c0.x));
    } else {
        // Gimbal lock: fold the Z rotation into X and pin Z to zero.
        t.rotation.x = glm::degrees(std::atan2(c0.y, c1.y));
        t.rotation.z = 0.0f;
    }
    return t;
}

}  // namespace ms
