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

#include <cstdint>
#include <string>
#include <vector>

#include "core/Math.hpp"

namespace ms {

// ---------------------------------------------------------------------------
// Rigid bodies and joints
//
// PMX stores an optional rigid-body simulation alongside the skeleton: a set
// of collision shapes attached to bones (hair, skirts, accessories) and the
// joints that constrain them. The structures here mirror that data in a
// format-neutral way, exactly as Bone mirrors a PMX bone. `Physics` consumes
// them; nothing here knows about the solver or the GPU.
//
// All positions and rotations are already in MSentara's right-handed space,
// the same conversion the mesh and bones get, so the solver can write a body's
// transform straight onto its bone.
// ---------------------------------------------------------------------------

// Collision shape, matching PMX's shape ids.
enum class RigidBodyShape : std::uint8_t {
    Sphere = 0,
    Box = 1,
    Capsule = 2,
};

// How the body is driven. PMX calls these "physics modes".
enum class RigidBodyMode : std::uint8_t {
    // Kinematic: the body snaps to its bone's animated transform and never
    // integrates. The anchor of a hair or cloth chain.
    Kinematic = 0,
    // Dynamic: free under gravity and impulses. The hair strands themselves.
    Dynamic = 1,
    // Dynamic + bone: dynamic, but the solved transform is blended back onto
    // the bone (MMD's "physics and bone position alignment").
    DynamicAndBone = 2,
};

// One collision body attached to a bone.
struct RigidBody {
    std::string name;
    int bone = -1;  // index into Skeleton::bones(), -1 for none

    // Collision filtering. A body collides with another only when they sit in
    // different groups and neither masks the other's group. `group` is 0..15,
    // `mask` is a 16-bit set of the groups this body refuses to collide with.
    std::uint8_t group = 0;
    std::uint16_t collision_mask = 0;

    RigidBodyShape shape = RigidBodyShape::Sphere;
    // Sphere: x is the radius. Box: half extents. Capsule: x is the radius and
    // y the half length along the local Y axis.
    vec3 size{0.0f, 0.0f, 0.0f};

    // Rest pose in model space (bone-local in PMX, resolved to model space by
    // the loader). A kinematic body follows its bone from here.
    vec3 position{0.0f, 0.0f, 0.0f};
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    float mass = 0.0f;
    float linear_damping = 0.0f;   // PMX "move attenuation"
    float angular_damping = 0.0f;  // PMX "rotation damping"
    float restitution = 0.0f;      // PMX "repulsion"
    float friction = 0.0f;

    RigidBodyMode mode = RigidBodyMode::Kinematic;

    bool dynamic() const { return mode != RigidBodyMode::Kinematic; }
};

// PMX joint type ids. Only the constraint's limits are honoured by the solver;
// the type selects which limits are meaningful.
enum class JointType : std::uint8_t {
    Spring6Dof = 0,
    Dof6 = 1,
    PointToPoint = 2,
    ConeTwist = 3,
    Slider = 4,
    Hinge = 5,
};

// A constraint between two rigid bodies. PMX stores one 6-DOF limit block for
// every type; the solver treats it as a soft, limited distance/angular spring,
// which is enough for hair and cloth while staying cheap.
struct Joint {
    std::string name;
    JointType type = JointType::Spring6Dof;
    int body_a = -1;  // index into the model's rigid bodies
    int body_b = -1;

    vec3 position{0.0f, 0.0f, 0.0f};  // joint frame, model space
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    vec3 translation_min{-0.0f, -0.0f, -0.0f};
    vec3 translation_max{0.0f, 0.0f, 0.0f};
    vec3 rotation_min{-0.0f, -0.0f, -0.0f};  // radians
    vec3 rotation_max{0.0f, 0.0f, 0.0f};

    vec3 spring_translation{0.0f, 0.0f, 0.0f};
    vec3 spring_rotation{0.0f, 0.0f, 0.0f};
};

// The rigid bodies and joints of one model.
struct RigidBodySet {
    std::vector<RigidBody> bodies;
    std::vector<Joint> joints;

    bool empty() const { return bodies.empty(); }
    std::size_t size() const { return bodies.size(); }
};

}  // namespace ms
