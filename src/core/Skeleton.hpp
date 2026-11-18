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

#include "core/Math.hpp"
#include "core/Mesh.hpp"
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

    // Fills in rest_global and inverse_bind by walking the hierarchy, and
    // caches the order in which that happened. Call after the last add_bone.
    void compute_rest();

    // Bone indices with every parent preceding its children. Empty until
    // compute_rest() has run.
    const std::vector<int>& eval_order() const { return order_; }

    // Radius of the rest pose around the origin, for framing the camera.
    float rest_radius() const;

    void clear();

private:
    std::vector<Bone> bones_;
    std::vector<int> order_;
};

// Per-bone animation state for one skeleton.
//
// `delta` is what the user authors (and what a future VMD track will drive):
// a transform applied *after* the bone's rest transform, so a rotation pivots
// around the bone's own origin, which is what posing intuitively means.
// The other three arrays are derived by evaluate_pose().
struct Pose {
    std::vector<Transform> delta;
    std::vector<mat4> local;
    std::vector<mat4> global;
    std::vector<mat4> palette;  // global * inverse_bind, the skinning matrix

    void resize(std::size_t bone_count);
    void reset();  // back to the rest pose
    std::size_t size() const { return delta.size(); }
    bool empty() const { return delta.empty(); }
};

// Recomputes pose.local/global/palette from pose.delta and the skeleton rest.
void evaluate_pose(const Skeleton& skeleton, Pose& pose);

// CPU skinning: transforms positions and normals by the pose palette.
// Vertices with no influence, and a pose that does not match the rig, are
// returned unchanged. Kept on the CPU deliberately — it needs no uniform
// array, so it behaves identically on desktop GL and WebGL2/GLES3.
MeshData skin_mesh(const MeshData& mesh, const Pose& pose);

}  // namespace ms
