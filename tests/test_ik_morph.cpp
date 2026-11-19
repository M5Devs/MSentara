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

// Rig-level tests for the Milestone 3 evaluation stages: bone morphs,
// append/grant transforms, and CCD-IK. Everything here builds a rig by hand,
// so no file format is involved and the numbers can be checked exactly.

#include <cmath>
#include <vector>

#include "TestCommon.hpp"
#include "core/Skeleton.hpp"

namespace {

using namespace ms;

// A two-link leg along -Y, all in the XZ plane is avoided on purpose so a
// planar rotation about Z is enough to reach any target in the XY plane:
//
//   hip  (0, 1, 0)  root
//   knee (0, 0, 0)  child of hip
//   foot (0,-1, 0)  child of knee   <- the IK effector
//
// An IK handle (the target) is a separate root bone that the solver chases.
Skeleton make_leg() {
    Skeleton skeleton;
    const int hip = skeleton.add_bone("hip", -1, vec3(0.0f, 1.0f, 0.0f));
    const int knee = skeleton.add_bone("knee", hip, vec3(0.0f, 0.0f, 0.0f));
    const int foot = skeleton.add_bone("foot", knee, vec3(0.0f, -1.0f, 0.0f));
    const int handle = skeleton.add_bone("handle", -1, vec3(0.0f, -1.0f, 0.0f));

    // The foot is the chain end (the effector) and the handle is the controller
    // (the goal) it is driven onto. Links are listed tip-first, as PMX stores
    // them.
    IkSolver solver;
    solver.effector_bone = foot;
    solver.goal_bone = handle;
    solver.iterations = 40;
    solver.limit_angle = 0.0f;  // no per-step clamp
    solver.links.push_back(IkLink{knee, false, vec3(0.0f), vec3(0.0f)});
    solver.links.push_back(IkLink{hip, false, vec3(0.0f), vec3(0.0f)});
    skeleton.ik_solvers().push_back(solver);
    skeleton.bone(foot).ik_solver = 0;

    skeleton.compute_rest();
    return skeleton;
}

void test_rest_unchanged_by_full_evaluate() {
    const Skeleton skeleton = make_leg();
    Pose pose;
    pose.resize(skeleton.size());
    evaluate_pose_full(skeleton, pose, {}, true);

    // The effector starts exactly on the handle, so the solver must not move
    // anything: a no-op is the correct result.
    MS_CHECK_NEAR(vec3(pose.global[2][3]), vec3(0.0f, -1.0f, 0.0f));
    MS_CHECK_NEAR(vec3(pose.global[3][3]), vec3(0.0f, -1.0f, 0.0f));
    MS_CHECK_NEAR(vec3(pose.global[0][3]), vec3(0.0f, 1.0f, 0.0f));
}

void test_ik_reaches_offset_target() {
    Skeleton skeleton = make_leg();
    // Move the handle sideways; the chain must bend so the foot lands on it.
    skeleton.bone(3).rest_position = vec3(0.6f, -0.8f, 0.0f);
    skeleton.bone(3).rest_local =
        glm::translate(mat4(1.0f), vec3(0.6f, -0.8f, 0.0f));
    skeleton.compute_rest();

    Pose pose;
    pose.resize(skeleton.size());
    evaluate_pose_full(skeleton, pose, {}, true);

    const vec3 effector(pose.global[2][3]);
    const vec3 target(pose.global[3][3]);
    const float error = glm::length(effector - target);
    MS_CHECK_MSG(error < 0.02f, "effector did not reach the IK target");
}

void test_ik_disabled_leaves_fk_pose() {
    Skeleton skeleton = make_leg();
    skeleton.bone(3).rest_position = vec3(0.6f, -0.8f, 0.0f);
    skeleton.bone(3).rest_local =
        glm::translate(mat4(1.0f), vec3(0.6f, -0.8f, 0.0f));
    skeleton.compute_rest();

    Pose pose;
    pose.resize(skeleton.size());
    evaluate_pose_full(skeleton, pose, {}, false);

    // With IK off the effector stays at its rest position, far from the target.
    MS_CHECK_NEAR(vec3(pose.global[2][3]), vec3(0.0f, -1.0f, 0.0f));
    MS_CHECK(glm::length(vec3(pose.global[2][3]) - vec3(pose.global[3][3])) >
             0.2f);
}

void test_ik_limit_clamps_knee() {
    Skeleton skeleton = make_leg();
    // Ask the knee to bend far more than its limit allows.
    skeleton.bone(3).rest_position = vec3(0.0f, 0.9f, 0.0f);
    skeleton.bone(3).rest_local =
        glm::translate(mat4(1.0f), vec3(0.0f, 0.9f, 0.0f));

    // A single hard limit: the knee may rotate at most 30 degrees about Z.
    IkSolver& solver = skeleton.ik_solvers()[0];
    solver.iterations = 50;
    solver.links[0].enable_limit = true;
    const float kLimit = glm::radians(30.0f);
    solver.links[0].limit_min = vec3(0.0f, 0.0f, -kLimit);
    solver.links[0].limit_max = vec3(0.0f, 0.0f, kLimit);
    skeleton.compute_rest();

    Pose pose;
    pose.resize(skeleton.size());
    evaluate_pose_full(skeleton, pose, {}, true);

    // The knee's total local rotation must respect the limit: read the angle
    // the chain bone ended up at and confirm it never exceeds 30 degrees.
    const quat knee_rot = pose.ik_rotation[1] *
                          euler_degrees_to_quat(pose.delta[1].rotation);
    const vec3 euler = glm::radians(quat_to_euler_degrees(knee_rot));
    MS_CHECK(std::fabs(euler.z) <= kLimit + 1.0e-3f);
}

void test_append_rotation_inherits_parent() {
    // parent rotates; child copies 50% of that rotation with no IK involved.
    Skeleton skeleton;
    const int parent = skeleton.add_bone("parent", -1, vec3(0.0f, 0.0f, 0.0f));
    const int child = skeleton.add_bone("child", -1, vec3(1.0f, 0.0f, 0.0f));
    skeleton.bone(child).has_append = true;
    skeleton.bone(child).append_parent = parent;
    skeleton.bone(child).append_weight = 0.5f;
    skeleton.bone(child).append_rotate = true;
    skeleton.compute_rest();

    Pose pose;
    pose.resize(skeleton.size());
    pose.delta[static_cast<std::size_t>(parent)].rotation =
        vec3(0.0f, 0.0f, 90.0f);  // quarter turn about Z
    evaluate_pose_full(skeleton, pose, {}, true);

    // The child has no parent link, so its global rotation is only the 50%
    // share of the parent's turn: 45 degrees about Z.
    const mat3 rot(pose.global[static_cast<std::size_t>(child)]);
    const vec3 x_axis = rot * vec3(1.0f, 0.0f, 0.0f);
    const float angle = glm::degrees(std::atan2(x_axis.y, x_axis.x));
    MS_CHECK(ms::test::nearly(angle, 45.0f, 0.5f));
}

void test_append_translation_inherits_parent() {
    // The child is a full parent of the parent in the hierarchy, so the parent
    // translation already reaches it; append-translation is checked on a bone
    // that is NOT a child, where the inherited share is the only motion.
    Skeleton skeleton;
    const int parent = skeleton.add_bone("parent", -1, vec3(0.0f, 0.0f, 0.0f));
    const int child = skeleton.add_bone("child", -1, vec3(1.0f, 0.0f, 0.0f));
    skeleton.bone(child).has_append = true;
    skeleton.bone(child).append_parent = parent;
    skeleton.bone(child).append_weight = 0.25f;
    skeleton.bone(child).append_translate = true;
    skeleton.compute_rest();

    Pose pose;
    pose.resize(skeleton.size());
    // Give the parent an *animated* translation. The child is a sibling, so
    // the only way it can move is by inheriting a share of that translation.
    pose.delta[static_cast<std::size_t>(parent)].position = vec3(4.0f, 0.0f, 0.0f);
    evaluate_pose_full(skeleton, pose, {}, true);

    // Child rest is at x=1; it gains a quarter of the parent's +4 translation.
    MS_CHECK_NEAR(vec3(pose.global[static_cast<std::size_t>(child)][3]),
                  vec3(2.0f, 0.0f, 0.0f));
}

void test_bone_morph_moves_bone() {
    Skeleton skeleton;
    const int bone = skeleton.add_bone("b", -1, vec3(0.0f, 0.0f, 0.0f));
    skeleton.compute_rest();

    Pose pose;
    pose.resize(skeleton.size());

    BoneMorphOffset offset;
    offset.bone = static_cast<std::uint32_t>(bone);
    offset.translation = vec3(0.0f, 3.0f, 0.0f);
    evaluate_pose_full(skeleton, pose, {offset}, true);

    MS_CHECK_NEAR(vec3(pose.global[0][3]), vec3(0.0f, 3.0f, 0.0f));
}

void test_vertex_morph_before_skinning() {
    // One bone at the origin; a vertex offset moves the vertex, then skinning
    // (which is the identity here) leaves it in place.
    Skeleton skeleton;
    skeleton.add_bone("root", -1, vec3(0.0f, 0.0f, 0.0f));
    skeleton.compute_rest();

    Pose pose;
    pose.resize(skeleton.size());
    evaluate_pose_full(skeleton, pose, {}, true);

    MeshData mesh;
    Vertex v;
    v.position = vec3(1.0f, 0.0f, 0.0f);
    v.set_influence(0, 0u, 1.0f);
    mesh.vertices.push_back(v);

    std::vector<vec3> offsets{vec3(0.0f, 2.0f, 0.0f)};
    const MeshData out = skin_mesh(mesh, pose, offsets);
    MS_CHECK_NEAR(out.vertices[0].position, vec3(1.0f, 2.0f, 0.0f));
}

}  // namespace

int main() {
    test_rest_unchanged_by_full_evaluate();
    test_ik_reaches_offset_target();
    test_ik_disabled_leaves_fk_pose();
    test_ik_limit_clamps_knee();
    test_append_rotation_inherits_parent();
    test_append_translation_inherits_parent();
    test_bone_morph_moves_bone();
    test_vertex_morph_before_skinning();
    return ms::test::summary("ik_morph");
}
