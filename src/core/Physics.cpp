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

#include "core/Physics.hpp"

#include <algorithm>
#include <cmath>

#include "core/Transform.hpp"

namespace ms {

namespace {

constexpr float kSlop = 1.0e-4f;
constexpr float kVelocityEps = 1.0e-6f;

mat4 world_of(const PhysicsBodyState& s) {
    return glm::translate(mat4(1.0f), s.position) * glm::mat4_cast(s.rotation);
}

// The lowest point of a body along world -Y, which is what the ground contact
// needs. Exact for a sphere and a support-function bound for a box/capsule.
float lowest_point(const RigidBody& body, const vec3& position,
                   const quat& rotation) {
    const mat3 basis = glm::mat3_cast(rotation);
    switch (body.shape) {
        case RigidBodyShape::Sphere:
            return position.y - body.size.x;
        case RigidBodyShape::Box: {
            float support = 0.0f;
            for (int a = 0; a < 3; ++a) {
                support += std::abs(basis[a].y) * body.size[a];
            }
            return position.y - support;
        }
        case RigidBodyShape::Capsule: {
            const float half = std::abs(basis[1].y) * body.size.y;
            return position.y - half - body.size.x;
        }
    }
    return position.y;
}

// Bounding-sphere radius, used for the cheap body-body overlap test.
float bounding_radius(const RigidBody& body) {
    switch (body.shape) {
        case RigidBodyShape::Sphere:
            return body.size.x;
        case RigidBodyShape::Box:
            return glm::length(body.size);
        case RigidBodyShape::Capsule:
            return body.size.x + body.size.y;
    }
    return 0.0f;
}

// MMD collision filtering: two bodies collide only when they are in different
// groups and neither lists the other's group in its non-collision mask.
bool groups_collide(const RigidBody& a, const RigidBody& b) {
    if (a.group == b.group) {
        return false;
    }
    if ((a.collision_mask & (1u << b.group)) != 0u) {
        return false;
    }
    if ((b.collision_mask & (1u << a.group)) != 0u) {
        return false;
    }
    return true;
}

// The smallest rotation that turns `from` onto `to`, or identity when they are
// (anti)parallel. Used to align a body's rest orientation with its segment.
quat rotation_between(const vec3& from, const vec3& to) {
    const float from_len = glm::length(from);
    const float to_len = glm::length(to);
    if (from_len < kVelocityEps || to_len < kVelocityEps) {
        return quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const vec3 a = from / from_len;
    const vec3 b = to / to_len;
    const float d = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if (d > 1.0f - 1.0e-6f) {
        return quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (d < -1.0f + 1.0e-6f) {
        // Opposite: rotate 180 degrees about any perpendicular axis.
        vec3 axis = glm::cross(a, vec3(0.0f, 1.0f, 0.0f));
        if (glm::length(axis) < 1.0e-4f) {
            axis = glm::cross(a, vec3(1.0f, 0.0f, 0.0f));
        }
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    const vec3 axis = glm::normalize(glm::cross(a, b));
    return glm::angleAxis(std::acos(d), axis);
}

}  // namespace

void Physics::build(const RigidBodySet& bodies, const Skeleton& skeleton) {
    source_ = bodies;
    bodies_ = bodies.bodies;
    joints_ = bodies.joints;

    const int bone_count = static_cast<int>(skeleton.size());
    states_.assign(bodies_.size(), PhysicsBodyState{});
    resolved_globals_.assign(static_cast<std::size_t>(std::max(bone_count, 0)),
                             mat4(1.0f));
    prev_positions_.assign(bodies_.size(), vec3(0.0f));
    contact_normals_.assign(bodies_.size(), vec3(0.0f));

    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        const RigidBody& body = bodies_[i];
        PhysicsBodyState& state = states_[i];

        state.bone = (body.bone >= 0 && body.bone < bone_count) ? body.bone : -1;
        state.dynamic = body.dynamic();
        state.dynamic_and_bone = (body.mode == RigidBodyMode::DynamicAndBone);

        state.rest_world = glm::translate(mat4(1.0f), body.position) *
                           glm::mat4_cast(body.rotation);
        state.bone_rest =
            state.bone >= 0 ? skeleton.bone(state.bone).rest_global : mat4(1.0f);
        state.bone_offset = glm::inverse(state.bone_rest) * state.rest_world;

        state.position = body.position;
        state.rest_position = body.position;
        state.rotation = body.rotation;
        state.rest_rotation = body.rotation;
        state.linear_velocity = vec3(0.0f);
        state.angular_velocity = vec3(0.0f);
        state.inv_mass =
            state.dynamic ? 1.0f / (body.mass > 0.0f ? body.mass : 1.0f) : 0.0f;
    }

    // Record each dynamic body's chain parent: the joint that hangs it off a
    // kinematic or another dynamic body. This drives the orientation pass and
    // gives the strand its swing.
    for (std::size_t j = 0; j < joints_.size(); ++j) {
        const Joint& joint = joints_[j];
        if (joint.body_a < 0 || joint.body_b < 0 ||
            joint.body_a >= static_cast<int>(states_.size()) ||
            joint.body_b >= static_cast<int>(states_.size())) {
            continue;
        }
        PhysicsBodyState& child = states_[static_cast<std::size_t>(joint.body_b)];
        if (child.dynamic) {
            child.parent_joint = static_cast<int>(j);
            child.parent_body = joint.body_a;
        }
    }
}

void Physics::clear() {
    source_ = RigidBodySet{};
    bodies_.clear();
    joints_.clear();
    states_.clear();
    resolved_globals_.clear();
    prev_positions_.clear();
    contact_normals_.clear();
}

void Physics::reset() {
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        PhysicsBodyState& state = states_[i];
        state.position = state.rest_position;
        state.rotation = state.rest_rotation;
        state.linear_velocity = vec3(0.0f);
        state.angular_velocity = vec3(0.0f);
    }
}

void Physics::integrate_kinematic(const Skeleton& skeleton, const Pose& pose) {
    (void)skeleton;
    for (PhysicsBodyState& state : states_) {
        if (state.dynamic) {
            continue;
        }
        mat4 world = state.rest_world;
        if (state.bone >= 0 &&
            state.bone < static_cast<int>(pose.global.size())) {
            world =
                pose.global[static_cast<std::size_t>(state.bone)] * state.bone_offset;
        }
        state.position = vec3(world[3]);
        const mat3 basis(glm::normalize(vec3(world[0])),
                         glm::normalize(vec3(world[1])),
                         glm::normalize(vec3(world[2])));
        state.rotation = glm::normalize(glm::quat_cast(basis));
    }
}

void Physics::solve_joint(int joint_index) {
    const Joint& joint = joints_[static_cast<std::size_t>(joint_index)];
    if (joint.body_a < 0 || joint.body_b < 0 ||
        joint.body_a >= static_cast<int>(states_.size()) ||
        joint.body_b >= static_cast<int>(states_.size())) {
        return;
    }
    PhysicsBodyState& a = states_[static_cast<std::size_t>(joint.body_a)];
    PhysicsBodyState& b = states_[static_cast<std::size_t>(joint.body_b)];

    const float inv_sum = a.inv_mass + b.inv_mass;
    if (inv_sum <= 0.0f) {
        return;  // two kinematic bodies cannot be corrected
    }

    // A chain link is modelled as a distance constraint between the two body
    // centres (the standard particle-chain approach): the centres keep the
    // spacing they had at rest, so a strand hangs and swings. The joint's
    // translation limits become the slack allowed on that distance, which is
    // how a loose joint lets a strand stretch a little.
    const float rest_distance =
        glm::length(a.rest_position - b.rest_position);
    const vec3 d = a.position - b.position;
    const float distance = glm::length(d);
    if (distance < kVelocityEps) {
        return;
    }

    float slack = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        slack = std::max(slack,
                         std::max(std::abs(joint.translation_min[axis]),
                                  std::abs(joint.translation_max[axis])));
    }
    const float lo = std::max(0.0f, rest_distance - slack);
    const float hi = rest_distance + slack;
    const float target = glm::clamp(distance, lo, hi);
    const float error = distance - target;
    if (std::abs(error) <= kSlop) {
        return;
    }

    // Position-based projection: move each body by its inverse-mass share so
    // the spacing equals the target. This removes the position error directly
    // and, because it is applied to positions, does not inject energy the way
    // a velocity-level correction does.
    const vec3 dir = d / distance;  // from b toward a
    b.position += dir * error * (b.inv_mass / inv_sum);
    a.position -= dir * error * (a.inv_mass / inv_sum);
}

void Physics::resolve_contacts() {
    for (std::size_t i = 0; i < states_.size(); ++i) {
        PhysicsBodyState& state = states_[i];
        if (!state.dynamic) {
            continue;
        }
        const RigidBody& body = bodies_[i];
        const vec3 before = state.position;

        // Ground plane.
        const float lowest = lowest_point(body, state.position, state.rotation);
        if (lowest < settings_.ground_y) {
            state.position.y += settings_.ground_y - lowest;
        }

        // Dynamic-into-kinematic collision (cheap bounding-sphere response).
        for (std::size_t j = 0; j < states_.size(); ++j) {
            if (j == i) {
                continue;
            }
            const PhysicsBodyState& other = states_[j];
            if (other.dynamic) {
                continue;
            }
            const RigidBody& other_body = bodies_[j];
            if (!groups_collide(body, other_body)) {
                continue;
            }
            const float r_sum =
                bounding_radius(body) + bounding_radius(other_body);
            if (r_sum <= 0.0f) {
                continue;
            }
            const vec3 delta = state.position - other.position;
            const float dist = glm::length(delta);
            if (dist >= r_sum || dist < 1.0e-6f) {
                continue;
            }
            state.position += (delta / dist) * (r_sum - dist);
        }

        contact_normals_[i] = state.position - before;
    }
}

void Physics::apply_contact_velocities() {
    // A contact correction that moved a body by `d` implies a normal velocity
    // of -d/h; cancel it (with restitution) so a body does not keep the speed
    // that drove it into the surface. Friction damps the tangential part.
    for (std::size_t i = 0; i < states_.size(); ++i) {
        if (!states_[i].dynamic) {
            continue;
        }
        const vec3 correction = contact_normals_[i];
        const float len = glm::length(correction);
        if (len <= kSlop) {
            continue;
        }
        const vec3 normal = correction / len;
        const RigidBody& body = bodies_[i];
        PhysicsBodyState& state = states_[i];

        const float vn = glm::dot(state.linear_velocity, normal);
        if (vn < 0.0f) {
            state.linear_velocity -= normal * vn * (1.0f + body.restitution);
        }
        const float keep = std::max(0.0f, 1.0f - body.friction);
        const vec3 tangential =
            state.linear_velocity - normal * glm::dot(state.linear_velocity, normal);
        state.linear_velocity -= tangential * (1.0f - keep);
    }
}

void Physics::align_orientations() {
    for (std::size_t i = 0; i < states_.size(); ++i) {
        PhysicsBodyState& state = states_[i];
        if (!state.dynamic || state.parent_body < 0 ||
            state.parent_body >= static_cast<int>(states_.size())) {
            continue;
        }
        const PhysicsBodyState& parent =
            states_[static_cast<std::size_t>(state.parent_body)];
        const Joint& joint =
            joints_[static_cast<std::size_t>(state.parent_joint)];

        // The strand segment runs from the parent's centre to this body's
        // centre. Comparing its rest direction with its live direction gives
        // the swing, which is applied to the body's rest orientation.
        const vec3 rest_segment = state.rest_position - parent.rest_position;
        const vec3 live_segment = state.position - parent.position;
        if (glm::length(rest_segment) < kVelocityEps ||
            glm::length(live_segment) < kVelocityEps) {
            continue;
        }

        const quat swing = rotation_between(rest_segment, live_segment);
        quat orientation = glm::normalize(swing * state.rest_rotation);

        // Clamp the relative rotation to the joint's angular limits, expressed
        // against the parent's current orientation.
        const quat rel =
            glm::normalize(glm::inverse(parent.rotation) * orientation);
        vec3 euler = glm::radians(quat_to_euler_degrees(rel));
        bool changed = false;
        for (int axis = 0; axis < 3; ++axis) {
            const float lo = joint.rotation_min[axis];
            const float hi = joint.rotation_max[axis];
            const float clamped = std::max(lo, std::min(hi, euler[axis]));
            if (std::abs(clamped - euler[axis]) > 1.0e-6f) {
                changed = true;
            }
            euler[axis] = clamped;
        }
        if (changed) {
            const quat clamped_rel = euler_degrees_to_quat(glm::degrees(euler));
            orientation = glm::normalize(parent.rotation * clamped_rel);
        }
        state.rotation = orientation;
    }
}

void Physics::substep(const Skeleton& skeleton, const Pose& pose, float h) {
    (void)skeleton;
    (void)pose;

    for (std::size_t i = 0; i < states_.size(); ++i) {
        PhysicsBodyState& state = states_[i];
        prev_positions_[i] = state.position;
        if (!state.dynamic || h <= 0.0f) {
            continue;
        }
        state.linear_velocity += settings_.gravity * h;
        state.position += state.linear_velocity * h;
    }

    for (int iteration = 0; iteration < settings_.solver_iterations;
         ++iteration) {
        for (int j = 0; j < static_cast<int>(joints_.size()); ++j) {
            solve_joint(j);
        }
        resolve_contacts();
    }

    // Recover velocities from the net position change (PBD), then damp them.
    // Damping is applied *after* recovery, not during prediction: recovery
    // re-derives the velocity from the solved positions, so damping the
    // predicted velocity alone would be overwritten and a swinging strand
    // would never lose energy.
    if (h > 0.0f) {
        for (std::size_t i = 0; i < states_.size(); ++i) {
            PhysicsBodyState& state = states_[i];
            if (!state.dynamic) {
                continue;
            }
            const RigidBody& body = bodies_[i];
            state.linear_velocity = (state.position - prev_positions_[i]) / h;
            state.linear_velocity *=
                std::exp(-std::max(body.linear_damping, 0.0f) * h);
            state.angular_velocity *=
                std::exp(-std::max(body.angular_damping, 0.0f) * h);
        }
    }
    apply_contact_velocities();

    align_orientations();

    // A body that has slowed to a crawl is parked, so a settled chain stops
    // creeping instead of drifting forever on residual constraint error.
    for (PhysicsBodyState& state : states_) {
        if (state.dynamic &&
            glm::length(state.linear_velocity) < settings_.sleep_velocity) {
            state.linear_velocity = vec3(0.0f);
            state.angular_velocity = vec3(0.0f);
        }
    }
}

void Physics::write_back(const Skeleton& skeleton, Pose& pose) {
    const int bone_count = static_cast<int>(skeleton.size());
    if (resolved_globals_.size() != static_cast<std::size_t>(bone_count)) {
        resolved_globals_.assign(
            static_cast<std::size_t>(std::max(bone_count, 0)), mat4(1.0f));
    }
    for (int i = 0; i < bone_count; ++i) {
        resolved_globals_[static_cast<std::size_t>(i)] =
            pose.global[static_cast<std::size_t>(i)];
    }

    // A body's bone takes the body's solved transform (mapped back through the
    // body's offset from the bone's rest pose). MMD's two dynamic modes differ
    // exactly here:
    //   * Dynamic          -> the bone follows the body completely.
    //   * DynamicAndBone   -> only the rotation is taken; the translation stays
    //                         the bone's animated one ("bone position
    //                         alignment").
    for (std::size_t i = 0; i < states_.size(); ++i) {
        const PhysicsBodyState& state = states_[i];
        if (!state.dynamic || state.bone < 0 || state.bone >= bone_count) {
            continue;
        }
        mat4 body_world = world_of(state);
        if (state.dynamic_and_bone) {
            const mat4& animated =
                pose.global[static_cast<std::size_t>(state.bone)];
            body_world[3] = animated[3];
        }
        resolved_globals_[static_cast<std::size_t>(state.bone)] =
            body_world * glm::inverse(state.bone_offset);
    }

    // Rebuild local/global/palette for every bone from the resolved globals,
    // so a bone whose parent was moved by physics is also correct.
    for (int index : skeleton.eval_order()) {
        const Bone& bone = skeleton.bones()[static_cast<std::size_t>(index)];
        const std::size_t b = static_cast<std::size_t>(index);
        const bool has_parent = bone.parent >= 0 && bone.parent < bone_count;
        const mat4 parent =
            has_parent ? resolved_globals_[static_cast<std::size_t>(bone.parent)]
                       : mat4(1.0f);
        const mat4 global = resolved_globals_[b];
        pose.global[b] = global;
        pose.local[b] = glm::inverse(parent) * global;
        pose.palette[b] = global * bone.inverse_bind;
    }
}

void Physics::step(const Skeleton& skeleton, Pose& pose, float dt) {
    if (states_.empty() || !enabled_) {
        return;
    }
    if (pose.size() != skeleton.size()) {
        return;
    }

    // Kinematic bodies follow the animated pose, which is constant across the
    // frame's sub-steps.
    integrate_kinematic(skeleton, pose);

    const float clamped_dt = std::min(dt, 0.1f);  // never simulate a huge jump
    const int substeps = std::max(settings_.substeps, 1);
    const float h =
        clamped_dt > 0.0f ? clamped_dt / static_cast<float>(substeps) : 0.0f;
    for (int s = 0; s < substeps; ++s) {
        substep(skeleton, pose, h);
    }

    write_back(skeleton, pose);
}

}  // namespace ms
