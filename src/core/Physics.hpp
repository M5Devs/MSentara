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

#include <cstddef>
#include <vector>

#include "core/Math.hpp"
#include "core/RigidBody.hpp"
#include "core/Skeleton.hpp"

namespace ms {

// ---------------------------------------------------------------------------
// A small, self-contained rigid-body solver for hair, skirts and accessories.
//
// This is deliberately NOT a general physics engine and has no external
// dependency (no Bullet): it targets the shape of the problem MMD models
// actually pose — a few dozen bodies hanging off animated bones — and is tuned
// to stay cheap and deterministic so it behaves the same on a low-end desktop
// and in WebAssembly.
//
// What it models:
//   * Kinematic bodies snap to their bone's animated transform (the anchor of
//     a chain, e.g. the head).
//   * Dynamic bodies integrate gravity and velocity/angular damping.
//   * Dynamic + bone bodies integrate the same way; only the write-back onto
//     the bone differs (MMD's "physics + bone position alignment").
//   * A joint links two bodies as a distance constraint between their centres
//     (the standard particle-chain approach for hair and cloth). The joint's
//     per-axis translation limits become the slack allowed on that distance,
//     and its per-axis rotation limits clamp the swing of the child segment.
//   * Contacts: every dynamic body stays above the ground plane, and dynamic
//     bodies push out of kinematic bodies (so hair rests on shoulders rather
//     than passing through them).
//
// Integration uses a fixed timestep with sub-stepping, so the result does not
// depend on the frame rate. Everything is preallocated: step() performs no
// heap allocation.
// ---------------------------------------------------------------------------

struct PhysicsSettings {
    vec3 gravity{0.0f, -9.81f, 0.0f};

    // One simulation step is split into this many fixed sub-steps per frame,
    // which keeps a long frame from tunnelling a body through the ground.
    int substeps = 4;
    // Constraint relaxation iterations per sub-step. More is stiffer.
    int solver_iterations = 8;

    // The ground plane: dynamic bodies are kept at or above y = ground_y.
    float ground_y = 0.0f;

    // Velocity below this magnitude is treated as zero, so a settled chain
    // stops jittering instead of creeping forever.
    float sleep_velocity = 2.0e-3f;
};

// Per-body simulation state. One per RigidBody; index-aligned with it.
//
// The solver keeps a body's *position* as the integrated state and derives its
// orientation from the chain it hangs from (the direction to its joint parent),
// which is what makes a hair or cloth strand swing and settle. Velocities are
// kept for damping, collision response and inspection.
struct PhysicsBodyState {
    int bone = -1;
    bool dynamic = false;
    bool dynamic_and_bone = false;

    mat4 rest_world{1.0f};   // rigid body's rest transform, model space
    mat4 bone_rest{1.0f};    // the bone's rest global
    mat4 bone_offset{1.0f};  // inverse(bone_rest) * rest_world

    // The joint that hangs this body off another (this body is the joint's B),
    // and that other body, or -1 when the body is a chain root. Used to derive
    // the body's orientation from the segment direction.
    int parent_joint = -1;
    int parent_body = -1;

    vec3 position{0.0f, 0.0f, 0.0f};       // current centre of mass
    vec3 rest_position{0.0f, 0.0f, 0.0f};  // centre of mass in the rest pose
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    quat rest_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    vec3 linear_velocity{0.0f, 0.0f, 0.0f};
    vec3 angular_velocity{0.0f, 0.0f, 0.0f};

    float inv_mass = 0.0f;
};

class Physics {
public:
    // Builds the solver state from a model's rigid bodies and its rig. Call
    // once after loading; a second call replaces the previous state. A model
    // with no rigid bodies leaves the solver empty, and step() is then a
    // no-op.
    void build(const RigidBodySet& bodies, const Skeleton& skeleton);

    // Drops all state.
    void clear();

    bool empty() const { return states_.empty(); }
    std::size_t size() const { return states_.size(); }

    bool enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }

    const PhysicsSettings& settings() const { return settings_; }
    PhysicsSettings& settings() { return settings_; }

    const RigidBody& body(int index) const {
        return bodies_[static_cast<std::size_t>(index)];
    }
    const Joint& joint(int index) const {
        return joints_[static_cast<std::size_t>(index)];
    }
    std::size_t joint_count() const { return joints_.size(); }

    // Live simulation state of one body, for inspection and tests.
    const PhysicsBodyState& state(int index) const {
        return states_[static_cast<std::size_t>(index)];
    }

    // Snaps every body back to its rest transform (dynamic bodies included)
    // and clears velocities. The rest transforms are already cached from build.
    void reset();

    // Advances the simulation by `dt` seconds and writes the resulting bone
    // globals into `pose`. `pose` must already hold the *animated* pose (the
    // output of evaluate_pose_full), because kinematic bodies follow it.
    //
    // Only bones driven by a dynamic body are overwritten; every other bone
    // keeps the animated transform it arrived with.
    void step(const Skeleton& skeleton, Pose& pose, float dt);

private:
    void substep(const Skeleton& skeleton, const Pose& pose, float h);
    void solve_joint(int joint_index);
    void resolve_contacts();
    void apply_contact_velocities();
    void integrate_kinematic(const Skeleton& skeleton, const Pose& pose);
    // Re-derives each dynamic body's orientation from its segment direction and
    // clamps the relative rotation to the joint's angular limits.
    void align_orientations();
    void write_back(const Skeleton& skeleton, Pose& pose);

    RigidBodySet source_;
    std::vector<RigidBody> bodies_;
    std::vector<Joint> joints_;
    std::vector<PhysicsBodyState> states_;

    // Preallocated scratch, so step() never allocates.
    std::vector<mat4> resolved_globals_;
    std::vector<vec3> prev_positions_;   // position at the start of a substep
    std::vector<vec3> contact_normals_;  // accumulated contact normal, zero if none

    PhysicsSettings settings_;
    bool enabled_ = true;
};

}  // namespace ms
