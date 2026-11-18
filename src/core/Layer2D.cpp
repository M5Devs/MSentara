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

#include "core/Layer2D.hpp"

#include <algorithm>
#include <numeric>

#include "core/Skeleton.hpp"

namespace ms {

namespace {

// The model-space global matrix of the bone a layer is bound to, or a null
// optional when the layer is unbound or the bone cannot be resolved (no
// skeleton/pose, an unknown name, or a pose that has not been evaluated yet).
// A resolvable bone replaces the layer's parent chain, so a cutout part
// follows the posed bone — including any CCD-IK that posed it.
bool bone_global(const Layer2D& layer, const Skeleton* skeleton,
                 const Pose* pose, mat4& out) {
    if (layer.bound_bone.empty() || skeleton == nullptr || pose == nullptr) {
        return false;
    }
    const int index = skeleton->find_bone(layer.bound_bone);
    if (index < 0 || static_cast<std::size_t>(index) >= pose->global.size()) {
        return false;
    }
    out = pose->global[static_cast<std::size_t>(index)];
    return true;
}

}  // namespace

mat4 Layer2D::local_matrix() const {
    // Translate the layer into place, rotate and scale about the origin, then
    // shift the pivot onto the origin so the rotation/scale happen about the
    // pivot rather than the image's corner.
    return pivot_matrix() * glm::translate(mat4(1.0f), vec3(-pivot, 0.0f));
}

mat4 Layer2D::pivot_matrix() const {
    mat4 m = glm::translate(mat4(1.0f), vec3(position, 0.0f));
    m = glm::rotate(m, glm::radians(rotation), vec3(0.0f, 0.0f, 1.0f));
    m = glm::scale(m, vec3(scale, 1.0f));
    return m;
}

LayerId Scene2D::add_layer(const std::string& name, LayerId parent) {
    Layer2D layer;
    layer.name = name;
    // A parent that does not resolve becomes a root, so a malformed project
    // cannot create a dangling link.
    layer.parent = (find(parent) != nullptr) ? parent : kInvalidLayer;
    layers_.push_back(std::move(layer));
    return static_cast<LayerId>(layers_.size()) - 1;
}

Layer2D* Scene2D::find(LayerId id) {
    if (id < 0 || id >= static_cast<LayerId>(layers_.size())) {
        return nullptr;
    }
    return &layers_[static_cast<std::size_t>(id)];
}

const Layer2D* Scene2D::find(LayerId id) const {
    if (id < 0 || id >= static_cast<LayerId>(layers_.size())) {
        return nullptr;
    }
    return &layers_[static_cast<std::size_t>(id)];
}

mat4 Scene2D::world_parent_matrix(LayerId id, const Skeleton* skeleton,
                                  const Pose* pose) const {
    const Layer2D* layer = find(id);
    if (layer == nullptr) {
        return mat4(1.0f);
    }
    // A resolvable bone binding replaces the parent chain entirely, so a cutout
    // part follows the posed (and CCD-IK-solved) bone. The bind offset is a
    // translation in the bone's own local frame.
    mat4 bone;
    if (bone_global(*layer, skeleton, pose, bone)) {
        return bone * glm::translate(mat4(1.0f), vec3(layer->bind_offset, 0.0f));
    }
    if (layer->parent == kInvalidLayer) {
        return mat4(1.0f);
    }
    return world_matrix(layer->parent, skeleton, pose);
}

mat4 Scene2D::pivot_parent_matrix(LayerId id, const Skeleton* skeleton,
                                  const Pose* pose) const {
    const Layer2D* layer = find(id);
    if (layer == nullptr) {
        return mat4(1.0f);
    }
    mat4 bone;
    if (bone_global(*layer, skeleton, pose, bone)) {
        return bone * glm::translate(mat4(1.0f), vec3(layer->bind_offset, 0.0f));
    }
    if (layer->parent == kInvalidLayer) {
        return mat4(1.0f);
    }
    return pivot_world_matrix(layer->parent, skeleton, pose);
}

mat4 Scene2D::world_matrix(LayerId id, const Skeleton* skeleton,
                           const Pose* pose) const {
    const Layer2D* layer = find(id);
    if (layer == nullptr) {
        return mat4(1.0f);
    }
    return world_parent_matrix(id, skeleton, pose) * layer->local_matrix();
}

mat4 Scene2D::pivot_world_matrix(LayerId id, const Skeleton* skeleton,
                                 const Pose* pose) const {
    const Layer2D* layer = find(id);
    if (layer == nullptr) {
        return mat4(1.0f);
    }
    return pivot_parent_matrix(id, skeleton, pose) * layer->pivot_matrix();
}

mat4 Scene2D::pivot_base_matrix(LayerId id, const Skeleton* skeleton,
                                const Pose* pose) const {
    // pivot_world_matrix() is exactly pivot_base * pivot_matrix(), so this is
    // the factor the gizmo inverts to recover a local transform from a
    // manipulated world matrix.
    return pivot_parent_matrix(id, skeleton, pose);
}

vec2 Scene2D::world_pivot(LayerId id, const Skeleton* skeleton,
                          const Pose* pose) const {
    const Layer2D* layer = find(id);
    if (layer == nullptr) {
        return vec2(0.0f);
    }
    // The pivot is a point of the image, so transform it as a point (w = 1).
    const vec4 p = world_matrix(id, skeleton, pose) * vec4(layer->pivot, 0.0f, 1.0f);
    return vec2(p.x, p.y);
}

std::vector<LayerId> Scene2D::draw_order() const {
    std::vector<LayerId> order(layers_.size());
    std::iota(order.begin(), order.end(), 0);
    // stable_sort keeps insertion order for equal z, which makes the result
    // deterministic (and therefore diffable) without needing a tiebreak key.
    std::stable_sort(order.begin(), order.end(),
                     [this](LayerId a, LayerId b) {
                         return layers_[static_cast<std::size_t>(a)].z_order <
                                layers_[static_cast<std::size_t>(b)].z_order;
                     });
    return order;
}

void Scene2D::select(LayerId id) {
    selected_ = (find(id) != nullptr) ? id : kInvalidLayer;
}

Layer2D* Scene2D::selected_layer() { return find(selected_); }

void Scene2D::clear() {
    layers_.clear();
    selected_ = kInvalidLayer;
}

}  // namespace ms
