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

#include <cstddef>
#include <vector>

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
}

void Pose::resize(std::size_t bone_count) {
    delta.assign(bone_count, Transform{});
    local.assign(bone_count, mat4(1.0f));
    global.assign(bone_count, mat4(1.0f));
    palette.assign(bone_count, mat4(1.0f));
}

void Pose::reset() {
    for (Transform& t : delta) {
        t = Transform{};
    }
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

MeshData skin_mesh(const MeshData& mesh, const Pose& pose) {
    MeshData out = mesh;
    const int bone_count = static_cast<int>(pose.palette.size());
    if (bone_count == 0) {
        return out;
    }

    for (std::size_t v = 0; v < out.vertices.size(); ++v) {
        const Vertex& in = mesh.vertices[v];
        Vertex& dst = out.vertices[v];

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
            position += weight * (skin * vec4(in.position, 1.0f));
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
        }
    }
    return out;
}

}  // namespace ms
