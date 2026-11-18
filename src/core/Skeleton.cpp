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

#include "core/Skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "core/Transform.hpp"

namespace ms {

namespace {

// Produces an order in which every parent precedes its children, so composing
// global transforms is a single linear pass.
//
// The traversal is iterative rather than recursive because a deep rig would
// otherwise risk the C stack. A malformed rig (parent index out of range, or
// a cycle) is tolerated instead of looping forever: the offending bone is
// simply resolved with whatever its parent currently holds.
std::vector<int> resolve_order(const std::vector<Bone>& bones) {
    const int count = static_cast<int>(bones.size());
    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(count));
    // 0 = unvisited, 1 = on the stack (in progress), 2 = emitted.
    std::vector<char> state(static_cast<std::size_t>(count), 0);

    std::vector<int> stack;
    for (int i = 0; i < count; ++i) {
        if (state[static_cast<std::size_t>(i)] != 0) {
            continue;
        }
        stack.push_back(i);
        state[static_cast<std::size_t>(i)] = 1;
        while (!stack.empty()) {
            const int current = stack.back();
            const int parent = bones[static_cast<std::size_t>(current)].parent;
            // Descend into a parent that has not been seen yet. A parent that
            // is already on the stack means the chain is cyclic; emitting the
            // current bone breaks the cycle instead of recursing forever.
            if (parent >= 0 && parent < count &&
                state[static_cast<std::size_t>(parent)] == 0) {
                stack.push_back(parent);
                state[static_cast<std::size_t>(parent)] = 1;
                continue;
            }
            state[static_cast<std::size_t>(current)] = 2;
            order.push_back(current);
            stack.pop_back();
        }
    }
    return order;
}

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

// Wraps an angle into [0, 2pi). MMD's solver works on raw angles and needs the
// shortest-arc difference, which only makes sense once they are normalised.
float normalize_angle(float angle) {
    float ret = std::fmod(angle, kTwoPi);
    if (ret < 0.0f) {
        ret += kTwoPi;
    }
    return ret;
}

// Signed shortest-arc difference a - b, in (-pi, pi].
float diff_angle(float a, float b) {
    float diff = normalize_angle(a) - normalize_angle(b);
    if (diff > kPi) {
        diff -= kTwoPi;
    } else if (diff < -kPi) {
        diff += kTwoPi;
    }
    return diff;
}

// Clamps `angle` into [min_angle, max_angle], choosing the representative
// nearest the interval when the raw angle lies outside it. Mirrors saba's
// ClampAngle so a knee limit behaves the way MMD expects.
float clamp_angle(float angle, float min_angle, float max_angle) {
    if (min_angle == max_angle) {
        return min_angle;
    }
    float ret = angle;
    while (ret < min_angle) {
        ret += kTwoPi;
    }
    if (ret < max_angle) {
        return ret;
    }
    while (ret > max_angle) {
        ret -= kTwoPi;
    }
    if (ret > min_angle) {
        return ret;
    }
    const float min_diff = std::abs(diff_angle(min_angle, ret));
    const float max_diff = std::abs(diff_angle(max_angle, ret));
    return (min_diff < max_diff) ? min_angle : max_angle;
}

}  // namespace

int Skeleton::add_bone(const std::string& name, int parent,
                       const vec3& rest_position) {
    const vec3 parent_position =
        (parent >= 0 && parent < static_cast<int>(bones_.size()))
            ? bones_[static_cast<std::size_t>(parent)].rest_position
            : vec3(0.0f);

    Bone bone;
    bone.name = name;
    bone.parent = parent;
    bone.rest_position = rest_position;
    bone.rest_local = glm::translate(mat4(1.0f), rest_position - parent_position);
    bones_.push_back(bone);
    return static_cast<int>(bones_.size()) - 1;
}

int Skeleton::add_bone(const std::string& name, int parent,
                       const mat4& rest_local) {
    Bone bone;
    bone.name = name;
    bone.parent = parent;
    bone.rest_local = rest_local;
    bone.rest_position = vec3(rest_local[3]);
    bones_.push_back(bone);
    return static_cast<int>(bones_.size()) - 1;
}

int Skeleton::find_bone(const std::string& name) const {
    for (std::size_t i = 0; i < bones_.size(); ++i) {
        if (bones_[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Skeleton::compute_rest() {
    order_ = resolve_order(bones_);
    const int count = static_cast<int>(bones_.size());
    for (int index : order_) {
        Bone& bone = bones_[static_cast<std::size_t>(index)];
        const bool has_parent = bone.parent >= 0 && bone.parent < count;
        const mat4 parent =
            has_parent ? bones_[static_cast<std::size_t>(bone.parent)].rest_global
                       : mat4(1.0f);
        bone.rest_global = parent * bone.rest_local;
        bone.inverse_bind = glm::inverse(bone.rest_global);
    }

    // Mark every bone a CCD-IK chain rotates, so the evaluation knows where to
    // apply the solver's rotation. MMD applies the IK rotation on the effector
    // and on each link, not only on the bone that owns the solver.
    for (Bone& bone : bones_) {
        if (bone.ik_solver >= 0) {
            bone.ik_chain_member = true;
        }
    }
    for (const IkSolver& solver : ik_solvers_) {
        for (const IkLink& link : solver.links) {
            if (link.bone >= 0 && link.bone < count) {
                bones_[static_cast<std::size_t>(link.bone)].ik_chain_member = true;
            }
        }
    }
}

float Skeleton::rest_radius() const {
    float radius = 0.0f;
    for (const Bone& bone : bones_) {
        radius = std::max(radius, glm::length(bone.rest_position));
    }
    return radius;
}

void Skeleton::clear() {
    bones_.clear();
    order_.clear();
    ik_solvers_.clear();
}

void Pose::resize(std::size_t bone_count) {
    delta.assign(bone_count, Transform{});
    ik_rotation.assign(bone_count, quat(1.0f, 0.0f, 0.0f, 0.0f));
    append_rotation.assign(bone_count, quat(1.0f, 0.0f, 0.0f, 0.0f));
    append_translation.assign(bone_count, vec3(0.0f));
    local.assign(bone_count, mat4(1.0f));
    global.assign(bone_count, mat4(1.0f));
    palette.assign(bone_count, mat4(1.0f));
}

void Pose::reset() {
    for (Transform& t : delta) {
        t = Transform{};
    }
    std::fill(ik_rotation.begin(), ik_rotation.end(),
              quat(1.0f, 0.0f, 0.0f, 0.0f));
    std::fill(append_rotation.begin(), append_rotation.end(),
              quat(1.0f, 0.0f, 0.0f, 0.0f));
    std::fill(append_translation.begin(), append_translation.end(), vec3(0.0f));
}

void evaluate_pose(const Skeleton& skeleton, Pose& pose) {
    const std::size_t count = skeleton.size();
    if (pose.size() != count) {
        pose.resize(count);
    }
    if (count == 0) {
        return;
    }

    const int bone_count = static_cast<int>(count);
    const auto evaluate_one = [&](int index) {
        const Bone& bone = skeleton.bones()[static_cast<std::size_t>(index)];
        const bool has_parent = bone.parent >= 0 && bone.parent < bone_count;
        const mat4 parent = has_parent
                                ? pose.global[static_cast<std::size_t>(bone.parent)]
                                : mat4(1.0f);
        const mat4 local =
            bone.rest_local *
            pose.delta[static_cast<std::size_t>(index)].matrix();
        pose.local[static_cast<std::size_t>(index)] = local;
        pose.global[static_cast<std::size_t>(index)] = parent * local;
        pose.palette[static_cast<std::size_t>(index)] =
            pose.global[static_cast<std::size_t>(index)] * bone.inverse_bind;
    };

    const std::vector<int>& order = skeleton.eval_order();
    if (order.size() == count) {
        for (int index : order) {
            evaluate_one(index);
        }
    } else {
        for (int index = 0; index < bone_count; ++index) {
            evaluate_one(index);
        }
    }
}

namespace {

// The animated rotation and translation of one bone before append/IK are
// folded in: its keyframe delta plus any bone-morph offset.
struct BoneAnim {
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    vec3 translation{0.0f, 0.0f, 0.0f};
};

// Per-bone morph translation/rotation, gathered once so the evaluation loop is
// a cheap lookup instead of a scan per bone.
std::vector<BoneAnim> gather_bone_morphs(
    std::size_t bone_count, const std::vector<BoneMorphOffset>& offsets) {
    std::vector<BoneAnim> morphs(bone_count);
    for (const BoneMorphOffset& offset : offsets) {
        if (offset.bone >= bone_count) {
            continue;
        }
        BoneAnim& m = morphs[offset.bone];
        m.translation += offset.translation;
        m.rotation = m.rotation * offset.rotation;
    }
    return morphs;
}

// Recomputes every bone's local/global/palette from the current pose arrays,
// folding in the bone-morph offsets, the IK rotation and the append share.
// A single linear pass in parent-before-child order.
void recompute_transforms(const Skeleton& skeleton, Pose& pose,
                          const std::vector<BoneAnim>& morphs) {
    const int bone_count = static_cast<int>(skeleton.size());
    for (int index : skeleton.eval_order()) {
        const Bone& bone = skeleton.bones()[static_cast<std::size_t>(index)];
        const std::size_t b = static_cast<std::size_t>(index);

        const Transform& d = pose.delta[b];
        const BoneAnim& morph = morphs[b];

        const quat anim_rot = euler_degrees_to_quat(d.rotation) * morph.rotation;
        const vec3 anim_trans = d.position + morph.translation;

        // MMD composes ik * animate * append (see saba's OnUpdateLocalTransform).
        quat r = anim_rot;
        if (bone.ik_chain_member) {
            r = pose.ik_rotation[b] * r;
        }
        if (bone.has_append && bone.append_rotate) {
            r = r * pose.append_rotation[b];
        }
        const vec3 t = anim_trans + pose.append_translation[b];

        const mat4 anim = glm::translate(mat4(1.0f), t) * glm::mat4_cast(r) *
                          glm::scale(mat4(1.0f), d.scale);
        const mat4 local = bone.rest_local * anim;

        const bool has_parent = bone.parent >= 0 && bone.parent < bone_count;
        const mat4 parent = has_parent
                                ? pose.global[static_cast<std::size_t>(bone.parent)]
                                : mat4(1.0f);

        pose.local[b] = local;
        pose.global[b] = parent * local;
        pose.palette[b] = pose.global[b] * bone.inverse_bind;
    }
}

// The full local rotation of a bone as the append pass reads it: its keyframe
// rotation, its bone morph, its own append share, and its IK rotation if the
// bone carries a solver. This is what an append child inherits.
quat animated_rotation(const Pose& pose, const Bone& bone,
                       const std::vector<BoneAnim>& morphs, std::size_t b) {
    quat r = euler_degrees_to_quat(pose.delta[b].rotation) * morphs[b].rotation;
    if (bone.has_append && bone.append_rotate) {
        r = r * pose.append_rotation[b];
    }
    if (bone.ik_chain_member) {
        r = pose.ik_rotation[b] * r;
    }
    return r;
}

// Bones ordered by deform depth (ascending), so a bone that inherits from an
// append parent is evaluated after it. MMD guarantees an append parent never
// sits deeper than its child, which is what makes this order sufficient.
std::vector<int> deform_order(const Skeleton& skeleton) {
    std::vector<int> order(skeleton.eval_order());
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return skeleton.bone(a).deform_depth < skeleton.bone(b).deform_depth;
    });
    return order;
}

// ---------------------------------------------------------------------------
// CCD-IK
//
// One solver rotates its chain's links so the effector (ik_bone) reaches the
// target bone. This is classic Cyclic Coordinate Descent: for each link, from
// the tip inward, rotate it so the effector-to-link vector lines up with the
// target-to-link vector, then repeat up to `iterations` times.
//
// Angle limits (a knee bending one way) are enforced by decomposing the link's
// resulting local rotation into Euler XYZ, clamping each axis to its limit, and
// recomposing. The per-step clamp keeps a single iteration from snapping the
// chain around when the target is nearly opposite the effector.
// ---------------------------------------------------------------------------
void solve_ik(const Skeleton& skeleton, Pose& pose,
              const std::vector<BoneAnim>& morphs, const IkSolver& solver) {
    const int bone_count = static_cast<int>(skeleton.size());
    if (solver.ik_bone < 0 || solver.ik_bone >= bone_count ||
        solver.target_bone < 0 || solver.target_bone >= bone_count ||
        solver.links.empty()) {
        return;
    }

    const std::size_t effector = static_cast<std::size_t>(solver.ik_bone);
    const std::size_t target = static_cast<std::size_t>(solver.target_bone);
    const float step_clamp =
        (solver.limit_angle > 0.0f) ? solver.limit_angle : kPi;

    // Per-link state that persists across iterations: the last clamped Euler
    // angle, so the limit clamp stays continuous instead of flipping.
    std::vector<vec3> prev_angle(solver.links.size(), vec3(0.0f));

    for (int iteration = 0; iteration < solver.iterations; ++iteration) {
        for (std::size_t li = 0; li < solver.links.size(); ++li) {
            const IkLink& link = solver.links[li];
            const int link_bone = link.bone;
            if (link_bone < 0 || link_bone >= bone_count ||
                link_bone == solver.ik_bone) {
                continue;  // a link equal to the effector has no defined axis
            }
            const std::size_t lb = static_cast<std::size_t>(link_bone);

            const vec3 ik_pos(pose.global[effector][3]);
            const vec3 target_pos(pose.global[target][3]);
            const mat4 inv_chain = glm::inverse(pose.global[lb]);

            const vec3 chain_ik = vec3(inv_chain * vec4(ik_pos, 1.0f));
            const vec3 chain_target = vec3(inv_chain * vec4(target_pos, 1.0f));

            const float ik_len = glm::length(chain_ik);
            const float target_len = glm::length(chain_target);
            if (ik_len < 1.0e-6f || target_len < 1.0e-6f) {
                continue;  // effector on the joint: no rotation defined
            }
            const vec3 ik_dir = chain_ik / ik_len;
            const vec3 target_dir = chain_target / target_len;

            float angle = std::acos(
                glm::clamp(glm::dot(target_dir, ik_dir), -1.0f, 1.0f));
            if (glm::degrees(angle) < 1.0e-3f) {
                continue;  // already aligned
            }
            angle = glm::clamp(angle, -step_clamp, step_clamp);

            // Rotate the effector direction onto the target direction. The axis
            // is cross(from, to) so a positive angle turns ik_dir toward
            // target_dir; a zero cross product means they are parallel.
            vec3 axis = glm::cross(ik_dir, target_dir);
            const float axis_len = glm::length(axis);
            if (axis_len < 1.0e-6f) {
                continue;  // parallel: any axis works, so do nothing
            }
            axis /= axis_len;

            const quat delta_rot = glm::angleAxis(angle, axis);
            const quat anim_rot =
                euler_degrees_to_quat(pose.delta[lb].rotation) * morphs[lb].rotation;
            quat chain_rot = pose.ik_rotation[lb] * anim_rot * delta_rot;

            if (link.enable_limit) {
                vec3 euler = glm::radians(quat_to_euler_degrees(chain_rot));
                vec3 lo = link.limit_min;
                vec3 hi = link.limit_max;
                vec3 clamped;
                for (int a = 0; a < 3; ++a) {
                    clamped[a] = (lo[a] == hi[a]) ? lo[a]
                                                   : clamp_angle(euler[a], lo[a], hi[a]);
                }
                // Keep the change per iteration within the step clamp so a knee
                // eases into its limit rather than snapping.
                clamped = glm::clamp(clamped - prev_angle[li], vec3(-step_clamp),
                                     vec3(step_clamp)) +
                          prev_angle[li];
                for (int a = 0; a < 3; ++a) {
                    if (lo[a] != hi[a]) {
                        clamped[a] = glm::clamp(clamped[a], lo[a], hi[a]);
                    }
                }
                prev_angle[li] = clamped;
                chain_rot = euler_degrees_to_quat(glm::degrees(clamped));
            }

            pose.ik_rotation[lb] = chain_rot * glm::inverse(anim_rot);

            // Later links read this link's updated global, so refresh now.
            recompute_transforms(skeleton, pose, morphs);
        }
    }
}

}  // namespace

void evaluate_pose_full(const Skeleton& skeleton, Pose& pose,
                        const std::vector<BoneMorphOffset>& bone_offsets,
                        bool enable_ik) {
    const std::size_t count = skeleton.size();
    if (pose.size() != count) {
        pose.resize(count);
    }
    if (count == 0) {
        return;
    }

    // Start from a clean IK/append state; the delta carries the keyframes.
    std::fill(pose.ik_rotation.begin(), pose.ik_rotation.end(),
              quat(1.0f, 0.0f, 0.0f, 0.0f));
    std::fill(pose.append_rotation.begin(), pose.append_rotation.end(),
              quat(1.0f, 0.0f, 0.0f, 0.0f));
    std::fill(pose.append_translation.begin(), pose.append_translation.end(),
              vec3(0.0f));

    const std::vector<BoneAnim> morphs = gather_bone_morphs(count, bone_offsets);

    // 1+2) keyframes and bone morphs, composed into local/global/palette.
    recompute_transforms(skeleton, pose, morphs);

    // 3) append/grant: each bone inherits a weighted share of its parent's
    //    motion, evaluated shallow-to-deep so a chain of append links resolves.
    bool any_append = false;
    for (int index : deform_order(skeleton)) {
        const Bone& bone = skeleton.bones()[static_cast<std::size_t>(index)];
        if (!bone.has_append || bone.append_parent < 0 ||
            bone.append_parent >= static_cast<int>(count)) {
            continue;
        }
        any_append = true;
        const std::size_t b = static_cast<std::size_t>(index);
        const std::size_t p = static_cast<std::size_t>(bone.append_parent);

        if (bone.append_rotate) {
            const quat parent_rot = animated_rotation(pose, bone, morphs, p);
            pose.append_rotation[b] = glm::slerp(
                quat(1.0f, 0.0f, 0.0f, 0.0f), parent_rot, bone.append_weight);
        }
        if (bone.append_translate) {
            const vec3 parent_trans = pose.delta[p].position +
                                      morphs[p].translation +
                                      pose.append_translation[p];
            pose.append_translation[b] = parent_trans * bone.append_weight;
        }
    }
    if (any_append) {
        recompute_transforms(skeleton, pose, morphs);
    }

    // 4) CCD-IK, one solver at a time. A later solver sees the pose an earlier
    //    one produced, matching MMD's sequential solver order.
    if (enable_ik) {
        for (const IkSolver& solver : skeleton.ik_solvers()) {
            if (solver.enabled) {
                solve_ik(skeleton, pose, morphs, solver);
            }
        }
    }

    // 5) final global/palette pass, ready for skinning.
    recompute_transforms(skeleton, pose, morphs);
}

MeshData skin_mesh(const MeshData& mesh, const Pose& pose) {
    return skin_mesh(mesh, pose, std::vector<vec3>{});
}

MeshData skin_mesh(const MeshData& mesh, const Pose& pose,
                   const std::vector<vec3>& position_offsets) {
    MeshData out = mesh;
    const int bone_count = static_cast<int>(pose.palette.size());
    const bool has_offsets = position_offsets.size() == mesh.vertices.size();

    if (bone_count == 0) {
        // No rig: a morph-only mesh still moves, so apply the offsets.
        if (has_offsets) {
            for (std::size_t v = 0; v < out.vertices.size(); ++v) {
                out.vertices[v].position += position_offsets[v];
            }
        }
        return out;
    }

    for (std::size_t v = 0; v < out.vertices.size(); ++v) {
        const Vertex& in = mesh.vertices[v];
        Vertex& dst = out.vertices[v];

        // Morph offsets are applied in rest space, before skinning, so an
        // expression deforms with the bones exactly like the base mesh does.
        const vec3 morphed =
            in.position + (has_offsets ? position_offsets[v] : vec3(0.0f));

        vec4 position(0.0f, 0.0f, 0.0f, 0.0f);
        vec3 normal(0.0f, 0.0f, 0.0f);
        float total = 0.0f;
        for (int k = 0; k < kMaxSkinInfluences; ++k) {
            const float weight = in.bone_weights[k];
            if (weight == 0.0f) {
                continue;
            }
            const int bone = static_cast<int>(in.bone_indices[k]);
            if (bone < 0 || bone >= bone_count) {
                continue;
            }
            const mat4& skin = pose.palette[static_cast<std::size_t>(bone)];
            position += weight * (skin * vec4(morphed, 1.0f));
            normal += weight * (mat3(skin) * in.normal);
            total += weight;
        }

        if (total > 1.0e-6f) {
            dst.position = vec3(position) / total;
            const vec3 averaged = normal / total;
            const float length_sq = glm::dot(averaged, averaged);
            dst.normal = (length_sq > 1.0e-12f)
                             ? averaged * glm::inversesqrt(length_sq)
                             : in.normal;
        } else if (has_offsets) {
            // Rigid vertex: the morph still moves it.
            dst.position = morphed;
        }
    }
    return out;
}

}  // namespace ms
