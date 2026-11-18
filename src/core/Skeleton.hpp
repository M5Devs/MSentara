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
#include <string>
#include <vector>

#include "core/IkTypes.hpp"
#include "core/Math.hpp"
#include "core/Mesh.hpp"
#include "core/Morph.hpp"
#include "core/Transform.hpp"

namespace ms {

// One joint of a skeletal rig.
//
// Three transforms matter and are easy to confuse:
//   rest_local   - the bone's rest transform relative to its parent
//   rest_global  - the bone's rest transform in model space (derived)
//   inverse_bind - inverse(rest_global), the matrix that moves a model-space
//                  vertex into this bone's local space (derived)
//
// Nothing here mentions PMX: a rig built by hand, or by a future 2D format,
// uses exactly the same structure.
struct Bone {
    std::string name;
    int parent = -1;  // index into Skeleton::bones(), -1 for a root

    // Model-space rest position. Informational, and what the UI shows.
    vec3 rest_position{0.0f, 0.0f, 0.0f};

    mat4 rest_local{1.0f};
    mat4 rest_global{1.0f};
    mat4 inverse_bind{1.0f};

    // Bones that never deform geometry (PMX's "display" / IK-only bones) are
    // skipped when skinning.
    bool deform = true;

    // PMX deform layer. Bones are evaluated in ascending order of this depth,
    // which is how MMD guarantees a parent is posed before a child that
    // inherits from it.
    int deform_depth = 0;

    // Append/Grant ("inherit") transform. When `has_append`, this bone copies a
    // weighted share of `append_parent`'s motion: its rotation, its
    // translation, or both. This is how MMD ties e.g. hair or a skirt to the
    // body without a full parent link.
    bool has_append = false;
    int append_parent = -1;
    float append_weight = 0.0f;
    bool append_rotate = false;
    bool append_translate = false;

    // Index into Skeleton::ik_solvers(), or -1. The bone that owns the solver
    // is the effector (the chain's tip).
    int ik_solver = -1;

    // True when this bone is rotated by a CCD-IK chain (as the effector or as
    // one of its links). MMD stores a per-node IK rotation, so the evaluation
    // applies `Pose::ik_rotation` on exactly these bones. Derived by
    // Skeleton::compute_rest().
    bool ik_chain_member = false;
};

// A hierarchy of bones in its rest pose.
class Skeleton {
public:
    // Appends a bone and returns its index. `rest_position` is in model space,
    // so the stored rest_local is the offset from the parent's rest position.
    // `parent` must already exist, or be -1.
    int add_bone(const std::string& name, int parent, const vec3& rest_position);

    // Appends a bone with an explicit rest-local matrix, for rigs that carry
    // bone orientation (PMX bones do not, but glTF joints do).
    int add_bone(const std::string& name, int parent, const mat4& rest_local);

    std::size_t size() const { return bones_.size(); }
    bool empty() const { return bones_.empty(); }

    Bone& bone(int index) { return bones_[static_cast<std::size_t>(index)]; }
    const Bone& bone(int index) const {
        return bones_[static_cast<std::size_t>(index)];
    }

    std::vector<Bone>& bones() { return bones_; }
    const std::vector<Bone>& bones() const { return bones_; }

    // Index of the bone named `name`, or -1 when the rig has no such bone. The
    // first match wins, which is what a name-keyed lookup (a 2D layer's
    // bound_bone, a project override) wants.
    int find_bone(const std::string& name) const;

    // Fills in rest_global and inverse_bind by walking the hierarchy, and
    // caches the order in which that happened. Call after the last add_bone.
    void compute_rest();

    // Bone indices with every parent preceding its children. Empty until
    // compute_rest() has run.
    const std::vector<int>& eval_order() const { return order_; }

    // CCD-IK chains owned by this rig. `Bone::ik_solver` indexes this.
    std::vector<IkSolver>& ik_solvers() { return ik_solvers_; }
    const std::vector<IkSolver>& ik_solvers() const { return ik_solvers_; }

    // Radius of the rest pose around the origin, for framing the camera.
    float rest_radius() const;

    void clear();

private:
    std::vector<Bone> bones_;
    std::vector<int> order_;
    std::vector<IkSolver> ik_solvers_;
};

// Per-bone animation state for one skeleton.
//
// `delta` is what the user authors (and what a VMD track drives): a transform
// applied *after* the bone's rest transform, so a rotation pivots around the
// bone's own origin, which is what posing intuitively means.
// The other arrays are derived by evaluate_pose().
//
// Two extra rotations exist because the full MMD pipeline composes them:
//   ik_rotation   - the rotation the IK solver added to this bone
//   append_rotation / append_translation - the share of an append parent's
//                   motion this bone inherited
// `local` is built as rest_local * delta, then the IK and append terms are
// folded in, matching MMD's AnimateRotate() * IK * append ordering.
struct Pose {
    std::vector<Transform> delta;
    std::vector<quat> ik_rotation;
    std::vector<quat> append_rotation;
    std::vector<vec3> append_translation;
    std::vector<mat4> local;
    std::vector<mat4> global;
    std::vector<mat4> palette;  // global * inverse_bind, the skinning matrix

    void resize(std::size_t bone_count);
    void reset();  // back to the rest pose
    std::size_t size() const { return delta.size(); }
    bool empty() const { return delta.empty(); }
};

// Recomputes pose.local/global/palette from pose.delta and the skeleton rest.
// This is the fast forward-kinematics-only path: it ignores append and IK.
void evaluate_pose(const Skeleton& skeleton, Pose& pose);

// The full MMD evaluation, in order:
//   1. keyframes   - pose.delta, already written by the caller (apply_clip)
//   2. bone morphs - `bone_offsets` added onto the delta (may be empty)
//   3. append/grant - each bone inherits a weighted share of its append parent
//   4. CCD-IK       - every enabled solver drives its chain onto its target
//   5. globals      - pose.global/palette, ready for skinning
//
// `bone_offsets` come from MorphSet::accumulate_offsets(). When the skeleton
// has no append links and no IK, this reduces to plain evaluate_pose().
void evaluate_pose_full(const Skeleton& skeleton, Pose& pose,
                        const std::vector<BoneMorphOffset>& bone_offsets,
                        bool enable_ik);

// CPU skinning: transforms positions and normals by the pose palette.
// Vertices with no influence, and a pose that does not match the rig, are
// returned unchanged. Kept on the CPU deliberately — it needs no uniform
// array, so it behaves identically on desktop GL and WebGL2/GLES3.
MeshData skin_mesh(const MeshData& mesh, const Pose& pose);

// As above, but first adds `position_offsets` (one per vertex, from the active
// vertex morphs) to the mesh positions. The offsets are applied before
// skinning so a facial expression deforms with the rig. An empty or
// wrong-sized `position_offsets` is ignored.
MeshData skin_mesh(const MeshData& mesh, const Pose& pose,
                   const std::vector<vec3>& position_offsets);

}  // namespace ms
