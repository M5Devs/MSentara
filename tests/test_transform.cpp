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

#include "core/Transform.hpp"

using namespace ms;
using namespace ms::test;

namespace {

void test_identity() {
    Transform t;
    MS_CHECK_NEAR(t.matrix(), mat4(1.0f));
}

void test_translation() {
    Transform t;
    t.position = vec3(1.0f, 2.0f, 3.0f);
    const mat4 m = t.matrix();
    MS_CHECK_NEAR(vec3(m[3]), vec3(1.0f, 2.0f, 3.0f));
}

void test_scale() {
    Transform t;
    t.scale = vec3(2.0f, 3.0f, 4.0f);
    const mat4 m = t.matrix();
    MS_CHECK_NEAR(vec3(m[0]), vec3(2.0f, 0.0f, 0.0f));
    MS_CHECK_NEAR(vec3(m[1]), vec3(0.0f, 3.0f, 0.0f));
    MS_CHECK_NEAR(vec3(m[2]), vec3(0.0f, 0.0f, 4.0f));
}

void test_rotation_x() {
    Transform t;
    t.rotation = vec3(90.0f, 0.0f, 0.0f);
    const mat4 m = t.matrix();
    // +90° about X maps +Y to +Z.
    MS_CHECK_NEAR(vec3(m[1]), vec3(0.0f, 0.0f, 1.0f));
    MS_CHECK_NEAR(vec3(m[2]), vec3(0.0f, -1.0f, 0.0f));
}

void test_roundtrip_simple() {
    Transform t;
    t.position = vec3(-2.0f, 0.5f, 7.0f);
    t.rotation = vec3(15.0f, 40.0f, -25.0f);
    t.scale = vec3(1.5f, 2.0f, 0.5f);

    const Transform back = Transform::from_matrix(t.matrix());
    MS_CHECK_NEAR(back.position, t.position);
    MS_CHECK_NEAR(back.scale, t.scale);
    MS_CHECK(nearly(back.rotation.x, t.rotation.x, 1e-3f));
    MS_CHECK(nearly(back.rotation.y, t.rotation.y, 1e-3f));
    MS_CHECK(nearly(back.rotation.z, t.rotation.z, 1e-3f));
}

void test_roundtrip_matrix() {
    // The strongest invariant: whatever angles we start with, recomposing the
    // decomposed TRS must reproduce the original matrix.
    for (float x : {-70.0f, 0.0f, 33.0f}) {
        for (float y : {-120.0f, 0.0f, 55.0f}) {
            for (float z : {10.0f, 0.0f, -95.0f}) {
                Transform t;
                t.position = vec3(3.0f, -1.0f, 2.0f);
                t.rotation = vec3(x, y, z);
                t.scale = vec3(1.0f, 1.0f, 1.0f);
                const mat4 original = t.matrix();
                const Transform back = Transform::from_matrix(original);
                MS_CHECK_NEAR(back.matrix(), original);
            }
        }
    }
}

void test_gimbal_lock_is_stable() {
    Transform t;
    t.rotation = vec3(0.0f, 90.0f, 0.0f);  // pitch at +90°, degenerate
    const mat4 original = t.matrix();
    const Transform back = Transform::from_matrix(original);
    MS_CHECK_NEAR(back.matrix(), original);
    MS_CHECK(nearly(back.rotation.z, 0.0f, 1e-4f));
}

void test_matches_closed_form_rotation() {
    // Independent check of the Euler convention: build Rx*Ry*Rz from the
    // closed-form trig expansion (not from glm::rotate) and confirm
    // Transform::matrix() agrees. This is the convention ImGuizmo's
    // RecomposeMatrixFromComponents uses, so the gizmo and the property panel
    // stay in agreement.
    const float x = 20.0f, y = 35.0f, z = -10.0f;
    const float cx = std::cos(glm::radians(x)), sx = std::sin(glm::radians(x));
    const float cy = std::cos(glm::radians(y)), sy = std::sin(glm::radians(y));
    const float cz = std::cos(glm::radians(z)), sz = std::sin(glm::radians(z));

    // R[row][col]
    const float r00 = cy * cz;
    const float r01 = -cy * sz;
    const float r02 = sy;
    const float r10 = cx * sz + sx * sy * cz;
    const float r11 = cx * cz - sx * sy * sz;
    const float r12 = -sx * cy;
    const float r20 = sx * sz - cx * sy * cz;
    const float r21 = sx * cz + cx * sy * sz;
    const float r22 = cx * cy;

    mat4 expected(1.0f);
    expected[0] = vec4(r00, r10, r20, 0.0f);  // column 0
    expected[1] = vec4(r01, r11, r21, 0.0f);  // column 1
    expected[2] = vec4(r02, r12, r22, 0.0f);  // column 2

    Transform t;
    t.rotation = vec3(x, y, z);
    MS_CHECK_NEAR(t.matrix(), expected);
}

}  // namespace

int main() {
    test_identity();
    test_translation();
    test_scale();
    test_rotation_x();
    test_roundtrip_simple();
    test_roundtrip_matrix();
    test_gimbal_lock_is_stable();
    test_matches_closed_form_rotation();
    return ms::test::summary("transform");
}
