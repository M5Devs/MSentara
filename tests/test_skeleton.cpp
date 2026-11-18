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

// Rig-level tests that do not involve any file format: hierarchy evaluation,
// the rest pose round trip, and CPU skinning.

#include "TestCommon.hpp"
#include "core/Skeleton.hpp"

namespace {

using namespace ms;

// A three-bone chain along +Y: root(0,0,0) -> mid(0,1,0) -> tip(0,2,0).
Skeleton make_chain() {
    Skeleton skeleton;
    skeleton.add_bone("root", -1, vec3(0.0f, 0.0f, 0.0f));
    skeleton.add_bone("mid", 0, vec3(0.0f, 1.0f, 0.0f));
    skeleton.add_bone("tip", 1, vec3(0.0f, 2.0f, 0.0f));
    skeleton.compute_rest();
    return skeleton;
}

void test_rest_transforms() {
    const Skeleton skeleton = make_chain();
    MS_CHECK(skeleton.size() == 3);

    // Each rest global is the accumulation of the chain from the origin.
    MS_CHECK_NEAR(vec3(skeleton.bone(0).rest_global[3]), vec3(0.0f, 0.0f, 0.0f));
    MS_CHECK_NEAR(vec3(skeleton.bone(1).rest_global[3]), vec3(0.0f, 1.0f, 0.0f));
    MS_CHECK_NEAR(vec3(skeleton.bone(2).rest_global[3]), vec3(0.0f, 2.0f, 0.0f));

    // inverse_bind * rest_global is the identity by definition.
    MS_CHECK_NEAR(skeleton.bone(2).inverse_bind * skeleton.bone(2).rest_global,
                  mat4(1.0f));

    // Parents must be evaluated before children.
    const std::vector<int>& order = skeleton.eval_order();
    MS_CHECK(order.size() == 3);
    if (order.size() == 3) {
        MS_CHECK(order[0] == 0);
        MS_CHECK(order[1] == 1);
        MS_CHECK(order[2] == 2);
    }
}

void test_rest_pose_is_identity() {
    const Skeleton skeleton = make_chain();
    Pose pose;
    evaluate_pose(skeleton, pose);

    // With no authored deltas, every palette matrix is the identity, so
    // skinning is a no-op.
    for (std::size_t i = 0; i < skeleton.size(); ++i) {
        MS_CHECK_NEAR(pose.palette[i], mat4(1.0f));
    }
}

void test_rotation_pivots_about_bone() {
    const Skeleton skeleton = make_chain();
    Pose pose;

    // Rotate "mid" (origin at y=1) by 90 degrees about Z. A point one unit
    // along +X from that pivot must end up one unit along +Y from it.
    pose.resize(skeleton.size());
    pose.delta[1].rotation = vec3(0.0f, 0.0f, 90.0f);
    evaluate_pose(skeleton, pose);

    const vec3 pivot(0.0f, 1.0f, 0.0f);
    const vec3 moved = vec3(pose.palette[1] * vec4(pivot + vec3(1.0f, 0.0f, 0.0f), 1.0f));
    MS_CHECK_NEAR(moved, vec3(0.0f, 2.0f, 0.0f));

    // The pivot itself is on the rotation axis, so it must not move.
    const vec3 origin = vec3(pose.palette[1] * vec4(pivot, 1.0f));
    MS_CHECK_NEAR(origin, pivot);
}

void test_child_follows_parent() {
    const Skeleton skeleton = make_chain();
    Pose pose;
    pose.resize(skeleton.size());

    // Translating the root must carry the whole chain with it.
    pose.delta[0].position = vec3(5.0f, 0.0f, 0.0f);
    evaluate_pose(skeleton, pose);

    MS_CHECK_NEAR(vec3(pose.global[2][3]), vec3(5.0f, 2.0f, 0.0f));
}

void test_skin_blends_two_bones() {
    Skeleton skeleton = make_chain();
    Pose pose;
    pose.resize(skeleton.size());

    MeshData mesh;
    Vertex v;
    v.position = vec3(1.0f, 1.0f, 0.0f);
    v.set_influence(0, 0, 0.5f);
    v.set_influence(1, 1, 0.5f);
    mesh.vertices.push_back(v);

    // Rest pose: skinning changes nothing.
    evaluate_pose(skeleton, pose);
    MeshData rest = skin_mesh(mesh, pose);
    MS_CHECK_NEAR(rest.vertices[0].position, vec3(1.0f, 1.0f, 0.0f));

    // Rotate bone 1 about Z. Bone 0 stays put, so the vertex lands halfway
    // between the unmoved point and the rotated one.
    pose.delta[1].rotation = vec3(0.0f, 0.0f, 90.0f);
    evaluate_pose(skeleton, pose);
    const MeshData posed = skin_mesh(mesh, pose);

    // Bone 1 maps (1,1,0) to (0,2,0); the 50/50 blend with the fixed (1,1,0)
    // is (0.5, 1.5, 0).
    MS_CHECK_NEAR(posed.vertices[0].position, vec3(0.5f, 1.5f, 0.0f));
}

void test_skin_leaves_unweighted_alone() {
    const Skeleton skeleton = make_chain();
    Pose pose;
    pose.resize(skeleton.size());
    pose.delta[1].rotation = vec3(0.0f, 0.0f, 90.0f);
    evaluate_pose(skeleton, pose);

    MeshData mesh;
    Vertex v;
    v.position = vec3(3.0f, 4.0f, 5.0f);
    mesh.vertices.push_back(v);

    const MeshData out = skin_mesh(mesh, pose);
    MS_CHECK_NEAR(out.vertices[0].position, vec3(3.0f, 4.0f, 5.0f));
}

void test_deep_hierarchy_and_cycles() {
    // A long chain must evaluate without recursion blowing the stack.
    Skeleton deep;
    int parent = -1;
    for (int i = 0; i < 2000; ++i) {
        // add_bone takes a model-space position, so each bone steps one unit
        // further along Y than its parent.
        parent = deep.add_bone("b" + std::to_string(i), parent,
                               vec3(0.0f, static_cast<float>(i + 1), 0.0f));
    }
    deep.compute_rest();
    MS_CHECK(deep.size() == 2000u);
    MS_CHECK(deep.eval_order().size() == 2000u);
    MS_CHECK_NEAR(vec3(deep.bone(1999).rest_global[3]), vec3(0.0f, 2000.0f, 0.0f));

    // A bone claiming a later bone as its parent would form a cycle. The
    // loader can never produce this, but evaluation must still terminate.
    Skeleton cyclic;
    cyclic.add_bone("a", -1, vec3(0.0f, 0.0f, 0.0f));
    cyclic.add_bone("b", 0, vec3(0.0f, 1.0f, 0.0f));
    cyclic.bone(0).parent = 1;  // hand-crafted cycle
    cyclic.compute_rest();
    MS_CHECK(cyclic.eval_order().size() == 2u);
}

void test_bounds() {
    MeshData mesh;
    for (const vec3& p : {vec3(-1.0f, -2.0f, -3.0f), vec3(1.0f, 2.0f, 3.0f)}) {
        Vertex v;
        v.position = p;
        mesh.vertices.push_back(v);
    }
    const Bounds bounds = compute_bounds(mesh);
    MS_CHECK(bounds.valid());
    MS_CHECK_NEAR(bounds.min, vec3(-1.0f, -2.0f, -3.0f));
    MS_CHECK_NEAR(bounds.max, vec3(1.0f, 2.0f, 3.0f));
    MS_CHECK_NEAR(bounds.center, vec3(0.0f, 0.0f, 0.0f));
    MS_CHECK_NEAR(bounds.radius, glm::length(vec3(1.0f, 2.0f, 3.0f)));

    MS_CHECK(!compute_bounds(MeshData{}).valid());
}

}  // namespace

int main() {
    test_rest_transforms();
    test_rest_pose_is_identity();
    test_rotation_pivots_about_bone();
    test_child_follows_parent();
    test_skin_blends_two_bones();
    test_skin_leaves_unweighted_alone();
    test_deep_hierarchy_and_cycles();
    test_bounds();
    return ms::test::summary("skeleton");
}
