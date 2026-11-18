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

// Two things are checked here:
//   1. The PMX rigid-body and joint sections parse to the right values, with
//      the same handedness conversion the mesh and bones get.
//   2. The lightweight solver behaves: a two-bone dynamic chain hangs off a
//      kinematic root under gravity, stays within its joint limits, respects
//      the ground, and can be reset or disabled.
//
// The PMX bytes are generated here (no copyrighted asset), and the simulation
// builds its rig by hand so the expected motion is exact.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "TestCommon.hpp"
#include "core/Physics.hpp"
#include "core/RigidBody.hpp"
#include "core/Skeleton.hpp"
#include "formats/PmxLoader.hpp"

namespace {

using namespace ms;

// ---------------------------------------------------------------------------
// A minimal PMX writer (UTF-8, 1-byte indices) that emits every section the
// parser walks, including display frames so the rigid-body section is reached
// from the correct offset.
// ---------------------------------------------------------------------------

class PmxWriter {
public:
    void u8(std::uint8_t v) { bytes_.push_back(v); }
    void i8(std::int8_t v) { bytes_.push_back(static_cast<std::uint8_t>(v)); }
    void i32(std::int32_t v) {
        const std::uint32_t u = static_cast<std::uint32_t>(v);
        for (int i = 0; i < 4; ++i) {
            bytes_.push_back(static_cast<std::uint8_t>((u >> (8 * i)) & 0xFFu));
        }
    }
    void f32(float v) {
        std::uint32_t raw = 0;
        std::memcpy(&raw, &v, sizeof(raw));
        i32(static_cast<std::int32_t>(raw));
    }
    void v3(const vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void text(const std::string& s) {
        i32(static_cast<std::int32_t>(s.size()));
        bytes_.insert(bytes_.end(), s.begin(), s.end());
    }

    const std::uint8_t* data() const { return bytes_.data(); }
    std::size_t size() const { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
};

// Writes a complete, valid PMX 2.0 with two bones, one display frame, two rigid
// bodies and one joint. Empty geometry keeps the test focused on the physics
// sections while still exercising the whole file walk.
std::vector<std::uint8_t> build_physics_pmx() {
    PmxWriter b;

    // --- Header ---
    const char* sig = "PMX ";
    for (int i = 0; i < 4; ++i) {
        b.u8(static_cast<std::uint8_t>(sig[i]));
    }
    b.f32(2.0f);
    b.u8(8);
    b.u8(1);  // UTF-8
    b.u8(0);  // additional vec4
    for (int i = 0; i < 6; ++i) {
        b.u8(1);  // every index size = 1 byte
    }
    b.text("PhysicsModel");
    b.text("");
    b.text("");
    b.text("");

    b.i32(0);  // vertices
    b.i32(0);  // surfaces
    b.i32(0);  // textures
    b.i32(0);  // materials

    // --- Bones: root at origin, hair child below it. ---
    b.i32(2);
    b.text("root");
    b.text("");
    b.v3(vec3(0.0f, 0.0f, 0.0f));
    b.i8(-1);
    b.i32(0);
    b.u8(0x00);
    b.u8(0x00);
    b.v3(vec3(0.0f, 1.0f, 0.0f));

    b.text("hair");
    b.text("");
    b.v3(vec3(0.0f, -0.5f, 0.0f));
    b.i8(0);
    b.i32(0);
    b.u8(0x00);
    b.u8(0x00);
    b.v3(vec3(0.0f, -1.5f, 0.0f));

    b.i32(0);  // morphs

    // --- Display frames: one frame with a bone and a morph entry. The morph
    // entry's index is a morph index even though there are none, which is what
    // a real file's UI list looks like; the parser only has to walk it. ---
    b.i32(1);
    b.text("Root");
    b.text("");
    b.u8(0);   // not a special frame
    b.i32(2);  // two frames
    b.u8(0);   // bone frame
    b.i8(0);   // bone index 0
    b.u8(0);   // bone frame
    b.i8(1);   // bone index 1

    // --- Rigid bodies ---
    b.i32(2);
    // Body 0: kinematic sphere on the root bone, at the origin.
    b.text("root_body");
    b.text("");
    b.i8(0);              // bone 0
    b.u8(0);              // group 0
    b.u8(0); b.u8(0);     // collision mask (u16)
    b.u8(0);              // shape: sphere
    b.v3(vec3(0.1f, 0.0f, 0.0f));  // radius
    b.v3(vec3(0.0f, 0.0f, 0.0f));  // position
    b.v3(vec3(0.0f, 0.0f, 0.0f));  // rotation
    b.f32(0.0f);          // mass
    b.f32(0.5f);          // move attenuation
    b.f32(0.5f);          // rotation damping
    b.f32(0.0f);          // repulsion
    b.f32(0.5f);          // friction
    b.u8(0);              // mode: kinematic

    // Body 1: dynamic capsule on the hair bone. Position carries a +Z so the
    // mirror is observable.
    b.text("hair_body");
    b.text("");
    b.i8(1);              // bone 1
    b.u8(1);              // group 1
    b.u8(0); b.u8(0);     // mask
    b.u8(2);              // shape: capsule
    b.v3(vec3(0.05f, 0.3f, 0.0f));
    b.v3(vec3(0.0f, -0.5f, 0.3f));  // position (z mirrors to -0.3)
    b.v3(vec3(0.0f, 0.0f, 0.5f));   // rotation about Z (mirror leaves it)
    b.f32(1.0f);          // mass
    b.f32(0.1f);          // move attenuation
    b.f32(0.1f);          // rotation damping
    b.f32(0.2f);          // repulsion
    b.f32(0.3f);          // friction
    b.u8(1);              // mode: dynamic

    // --- Joints ---
    b.i32(1);
    b.text("hair_joint");
    b.text("");
    b.u8(0);              // type: spring 6DOF
    b.i8(0);              // body A
    b.i8(1);              // body B
    b.v3(vec3(0.0f, -0.25f, 0.2f));  // position (z mirrors)
    b.v3(vec3(0.0f, 0.0f, 0.0f));    // rotation
    b.v3(vec3(-0.1f, -0.2f, -0.1f)); // translation min
    b.v3(vec3(0.1f, 0.2f, 0.1f));    // translation max
    b.v3(vec3(-0.5f, -0.5f, -0.5f)); // rotation min
    b.v3(vec3(0.5f, 0.5f, 0.5f));    // rotation max
    b.v3(vec3(1.0f, 1.0f, 1.0f));    // translation spring
    b.v3(vec3(1.0f, 1.0f, 1.0f));    // rotation spring

    return std::vector<std::uint8_t>(b.data(), b.data() + b.size());
}

void test_pmx_rigid_bodies_and_joints() {
    const std::vector<std::uint8_t> bytes = build_physics_pmx();
    const PmxResult result = parse_pmx(bytes.data(), bytes.size());
    MS_CHECK(result.ok);
    MS_CHECK_MSG(result.error.empty(), result.error);
    if (!result.ok) {
        return;
    }

    const RigidBodySet& set = result.model.rigid_bodies;
    MS_CHECK(set.bodies.size() == 2);
    MS_CHECK(set.joints.size() == 1);

    // Kinematic anchor.
    const RigidBody& root = set.bodies[0];
    MS_CHECK(root.name == "root_body");
    MS_CHECK(root.bone == 0);
    MS_CHECK(root.shape == RigidBodyShape::Sphere);
    MS_CHECK(root.mode == RigidBodyMode::Kinematic);
    MS_CHECK(!root.dynamic());
    MS_CHECK_NEAR(root.size.x, 0.1f);

    // Dynamic strand.
    const RigidBody& hair = set.bodies[1];
    MS_CHECK(hair.bone == 1);
    MS_CHECK(hair.shape == RigidBodyShape::Capsule);
    MS_CHECK(hair.mode == RigidBodyMode::Dynamic);
    MS_CHECK(hair.dynamic());
    MS_CHECK(hair.group == 1u);
    MS_CHECK_NEAR(hair.size.y, 0.3f);
    MS_CHECK_NEAR(hair.mass, 1.0f);
    MS_CHECK_NEAR(hair.linear_damping, 0.1f);
    MS_CHECK_NEAR(hair.restitution, 0.2f);
    MS_CHECK_NEAR(hair.friction, 0.3f);
    // Position Z is mirrored; X and Y are unchanged.
    MS_CHECK_NEAR(hair.position, vec3(0.0f, -0.5f, -0.3f));
    // The rotation is about Z, which the mirror leaves unchanged.
    const quat expected_rot = glm::angleAxis(0.5f, vec3(0.0f, 0.0f, 1.0f));
    MS_CHECK(ms::test::nearly(hair.rotation, expected_rot, 1e-3f));

    // Joint.
    const Joint& joint = set.joints[0];
    MS_CHECK(joint.name == "hair_joint");
    MS_CHECK(joint.type == JointType::Spring6Dof);
    MS_CHECK(joint.body_a == 0);
    MS_CHECK(joint.body_b == 1);
    MS_CHECK_NEAR(joint.position, vec3(0.0f, -0.25f, -0.2f));
    MS_CHECK_NEAR(joint.translation_min, vec3(-0.1f, -0.2f, -0.1f));
    MS_CHECK_NEAR(joint.translation_max, vec3(0.1f, 0.2f, 0.1f));
    MS_CHECK_NEAR(joint.rotation_min, vec3(-0.5f, -0.5f, -0.5f));
    MS_CHECK_NEAR(joint.rotation_max, vec3(0.5f, 0.5f, 0.5f));
    MS_CHECK_NEAR(joint.spring_translation, vec3(1.0f, 1.0f, 1.0f));
    MS_CHECK_NEAR(joint.spring_rotation, vec3(1.0f, 1.0f, 1.0f));
}

// ---------------------------------------------------------------------------
// Simulation helpers
// ---------------------------------------------------------------------------

// root(0,0,0) -> hair1(0.5,0,0) -> hair2(1,0,0): a two-segment hair or cloth
// strand laid out sideways, so gravity swings it down to hang below the root.
Skeleton make_chain_skeleton() {
    Skeleton skeleton;
    skeleton.add_bone("root", -1, vec3(0.0f, 0.0f, 0.0f));
    skeleton.add_bone("hair1", 0, vec3(0.5f, 0.0f, 0.0f));
    skeleton.add_bone("hair2", 1, vec3(1.0f, 0.0f, 0.0f));
    skeleton.compute_rest();
    return skeleton;
}

RigidBody make_body(const std::string& name, int bone, const vec3& position,
                    RigidBodyMode mode, float radius) {
    RigidBody body;
    body.name = name;
    body.bone = bone;
    body.shape = RigidBodyShape::Sphere;
    body.size = vec3(radius, 0.0f, 0.0f);
    body.position = position;
    body.mass = (mode == RigidBodyMode::Kinematic) ? 0.0f : 1.0f;
    // Strong attenuation, as a hair strand uses, so the swing dies out.
    body.linear_damping = 3.0f;
    body.angular_damping = 3.0f;
    body.mode = mode;
    return body;
}

Joint make_joint(int a, int b, const vec3& position, float limit) {
    Joint joint;
    joint.type = JointType::Spring6Dof;
    joint.body_a = a;
    joint.body_b = b;
    joint.position = position;
    joint.translation_min = vec3(-limit, -limit, -limit);
    joint.translation_max = vec3(limit, limit, limit);
    joint.rotation_min = vec3(-1.0f, -1.0f, -1.0f);
    joint.rotation_max = vec3(1.0f, 1.0f, 1.0f);
    return joint;
}

bool all_finite(const Pose& pose) {
    for (const mat4& m : pose.global) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (!std::isfinite(m[c][r])) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Steps the solver at a fixed 60 Hz for `seconds` and returns the final pose.
Pose run(Physics& physics, const Skeleton& skeleton, float seconds) {
    Pose pose;
    pose.resize(skeleton.size());
    evaluate_pose(skeleton, pose);
    const float dt = 1.0f / 60.0f;
    const int steps = static_cast<int>(seconds / dt);
    for (int i = 0; i < steps; ++i) {
        physics.step(skeleton, pose, dt);
    }
    return pose;
}

void test_chain_settles_and_stays_attached() {
    const Skeleton skeleton = make_chain_skeleton();

    RigidBodySet bodies;
    bodies.bodies.push_back(
        make_body("root_body", 0, vec3(0.0f, 0.0f, 0.0f),
                  RigidBodyMode::Kinematic, 0.1f));
    bodies.bodies.push_back(make_body("hair1_body", 1, vec3(0.5f, 0.0f, 0.0f),
                                      RigidBodyMode::Dynamic, 0.05f));
    bodies.bodies.push_back(make_body("hair2_body", 2, vec3(1.0f, 0.0f, 0.0f),
                                      RigidBodyMode::Dynamic, 0.05f));
    // A tight limit, as a real hair joint uses: the chain must stay together.
    bodies.joints.push_back(make_joint(0, 1, vec3(0.25f, 0.0f, 0.0f), 0.05f));
    bodies.joints.push_back(make_joint(1, 2, vec3(0.75f, 0.0f, 0.0f), 0.05f));

    Physics physics;
    physics.build(bodies, skeleton);
    physics.settings().gravity = vec3(0.0f, -9.81f, 0.0f);
    physics.settings().ground_y = -5.0f;  // out of the way for this test
    physics.reset();

    const Pose pose = run(physics, skeleton, 3.0f);

    MS_CHECK(all_finite(pose));

    // The kinematic root does not move.
    const vec3 root_pos(pose.global[0][3]);
    MS_CHECK_NEAR(root_pos, vec3(0.0f, 0.0f, 0.0f));

    // Gravity swung the sideways strand down until it hangs below the root:
    // the first segment is nearly vertical, and the second below it.
    const vec3 hair1_pos(pose.global[1][3]);
    const vec3 hair2_pos(pose.global[2][3]);
    MS_CHECK(hair1_pos.y < -0.45f);
    MS_CHECK(std::abs(hair1_pos.x) < 0.15f);  // settled hanging, not sideways
    MS_CHECK(hair2_pos.y < hair1_pos.y);

    // Each joint keeps the 0.5 centre spacing (with the 0.05 limit as slack),
    // so the chain stays attached.
    MS_CHECK(glm::length(hair1_pos - root_pos) < 0.5f + 0.06f);
    MS_CHECK(glm::length(hair2_pos - hair1_pos) < 0.5f + 0.06f);

    // The chain settled: its speeds have died down.
    MS_CHECK(glm::length(physics.state(1).linear_velocity) < 0.01f);
    MS_CHECK(glm::length(physics.state(2).linear_velocity) < 0.01f);
}

void test_ground_plane_stops_a_falling_body() {
    const Skeleton skeleton = make_chain_skeleton();

    RigidBodySet bodies;
    bodies.bodies.push_back(
        make_body("root_body", 0, vec3(0.0f, 0.0f, 0.0f),
                  RigidBodyMode::Kinematic, 0.1f));
    // A single dynamic strand whose rest position is high above the floor.
    bodies.bodies.push_back(make_body("hair1_body", 1, vec3(0.5f, 0.5f, 0.0f),
                                      RigidBodyMode::Dynamic, 0.1f));
    bodies.joints.push_back(make_joint(0, 1, vec3(0.25f, 0.25f, 0.0f), 2.0f));

    Physics physics;
    physics.build(bodies, skeleton);
    physics.settings().ground_y = -0.5f;
    physics.reset();

    const Pose pose = run(physics, skeleton, 3.0f);
    MS_CHECK(all_finite(pose));

    // The body came to rest on the floor rather than passing through it. Its
    // sphere radius is 0.1, so the centre sits a radius above the plane.
    const float y = physics.state(1).position.y;
    MS_CHECK(y > physics.settings().ground_y - 1.0e-3f);
    MS_CHECK(ms::test::nearly(y, physics.settings().ground_y + 0.1f, 0.05f));
}

void test_reset_and_disabled() {
    const Skeleton skeleton = make_chain_skeleton();

    RigidBodySet bodies;
    bodies.bodies.push_back(
        make_body("root_body", 0, vec3(0.0f, 0.0f, 0.0f),
                  RigidBodyMode::Kinematic, 0.1f));
    bodies.bodies.push_back(make_body("hair1_body", 1, vec3(0.5f, 0.0f, 0.0f),
                                      RigidBodyMode::Dynamic, 0.05f));
    bodies.joints.push_back(make_joint(0, 1, vec3(0.25f, 0.0f, 0.0f), 0.05f));

    Physics physics;
    physics.build(bodies, skeleton);
    physics.settings().ground_y = -5.0f;

    // Disabled: a step must not move a dynamic body.
    physics.set_enabled(false);
    physics.reset();
    {
        Pose pose;
        pose.resize(skeleton.size());
        evaluate_pose(skeleton, pose);
        for (int i = 0; i < 120; ++i) {
            physics.step(skeleton, pose, 1.0f / 60.0f);
        }
        MS_CHECK_NEAR(physics.state(1).position, vec3(0.5f, 0.0f, 0.0f));
        MS_CHECK(physics.state(1).linear_velocity == vec3(0.0f));
    }

    // Enabled: it falls, then a reset returns it to rest with zero velocity.
    physics.set_enabled(true);
    physics.reset();
    run(physics, skeleton, 1.0f);
    MS_CHECK(physics.state(1).position.y < -0.1f);

    physics.reset();
    MS_CHECK_NEAR(physics.state(1).position, vec3(0.5f, 0.0f, 0.0f));
    MS_CHECK(physics.state(1).linear_velocity == vec3(0.0f));
    MS_CHECK(physics.state(1).angular_velocity == vec3(0.0f));
}

void test_dynamic_vs_dynamic_and_bone_write_back() {
    // MMD's two dynamic modes differ only in what the bone takes from the
    // solved body:
    //   * Dynamic        -> the bone follows the body completely.
    //   * DynamicAndBone -> only the rotation is taken; the bone keeps its
    //                       animated translation ("bone position alignment").
    // This test drives the same falling body in both modes and checks that the
    // write-back differs exactly there.

    const Skeleton skeleton = make_chain_skeleton();

    const auto run_mode = [&](RigidBodyMode mode, float& out_translation_y,
                              vec3& out_velocity) {
        RigidBodySet bodies;
        bodies.bodies.push_back(
            make_body("root_body", 0, vec3(0.0f, 0.0f, 0.0f),
                      RigidBodyMode::Kinematic, 0.1f));
        bodies.bodies.push_back(
            make_body("hair_body", 1, vec3(0.5f, 0.0f, 0.0f), mode, 0.05f));
        bodies.joints.push_back(
            make_joint(0, 1, vec3(0.25f, 0.0f, 0.0f), 2.0f));

        Physics physics;
        physics.build(bodies, skeleton);
        physics.settings().ground_y = -50.0f;  // nothing stops the fall
        physics.reset();

        const Pose pose = run(physics, skeleton, 0.5f);
        // The bone's world Y comes from the pose; the body's own solved Y is
        // the physical position. They agree for Dynamic and diverge for
        // DynamicAndBone.
        out_translation_y = pose.global[1][3].y;
        out_velocity = physics.state(1).linear_velocity;
    };

    float dynamic_y = 0.0f;
    float dynamic_bone_y = 0.0f;
    vec3 dynamic_v(0.0f);
    vec3 dynamic_bone_v(0.0f);
    run_mode(RigidBodyMode::Dynamic, dynamic_y, dynamic_v);
    run_mode(RigidBodyMode::DynamicAndBone, dynamic_bone_y, dynamic_bone_v);

    // Both bodies fell the same distance, so the underlying simulation is
    // identical...
    MS_CHECK_NEAR(dynamic_v, dynamic_bone_v);
    MS_CHECK(dynamic_v.y < -0.1f);  // it really did fall

    // ...but only the Dynamic bone followed the body down. The DynamicAndBone
    // bone kept its animated (rest) translation of y = 0.
    MS_CHECK(dynamic_y < -0.1f);
    MS_CHECK(ms::test::nearly(dynamic_bone_y, 0.0f, 1.0e-4f));
}

void test_empty_physics_is_inert() {
    const Skeleton skeleton = make_chain_skeleton();
    Physics physics;
    physics.build(RigidBodySet{}, skeleton);
    MS_CHECK(physics.empty());

    Pose pose;
    pose.resize(skeleton.size());
    evaluate_pose(skeleton, pose);
    const mat4 before = pose.global[1];
    for (int i = 0; i < 60; ++i) {
        physics.step(skeleton, pose, 1.0f / 60.0f);
    }
    // No bodies means the pose is untouched.
    MS_CHECK(ms::test::nearly(pose.global[1], before));
}

}  // namespace

int main() {
    test_pmx_rigid_bodies_and_joints();
    test_chain_settles_and_stays_attached();
    test_ground_plane_stops_a_falling_body();
    test_reset_and_disabled();
    test_dynamic_vs_dynamic_and_bone_write_back();
    test_empty_physics_is_inert();
    return ms::test::summary("physics");
}
