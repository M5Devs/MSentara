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

#include "ui/ModelInstance.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include "formats/PmxLoader.hpp"
#include "platform/ImageIO.hpp"

namespace ms {

namespace {

// Everything up to and including the last separator; PMX texture references
// are relative to the model file's own directory.
std::string directory_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return {};
    }
    return path.substr(0, slash);
}

}  // namespace

bool ModelInstance::load(const std::string& path, Renderer& renderer,
                         std::string& error) {
    clear(renderer);

    PmxResult result = load_pmx(path);
    if (!result.ok) {
        error = result.error;
        return false;
    }
    model_ = std::move(result.model);
    source_path_ = path;

    // Decode and upload every texture the model references. A missing file is
    // not fatal: that material falls back to the shared white texture.
    const std::string model_dir = directory_of(path);
    textures_.reserve(model_.texture_paths.size());
    for (const std::string& reference : model_.texture_paths) {
        const std::string resolved =
            resolve_texture_path(model_dir, reference);
        const TextureData decoded = load_texture_file(resolved);
        textures_.push_back(renderer.create_texture(decoded));
    }

    indices_ = renderer.create_index_buffer(model_.mesh.indices);
    // Start at the mesh's own size; upload_vertices grows it if the skinning
    // pass ever produces more (it does not today, but this keeps the contract
    // honest).
    vertices_ = renderer.create_dynamic_mesh(
        static_cast<int>(model_.mesh.vertices.size()));

    pose_.resize(model_.skeleton.size());
    evaluate();
    selected_bone_ = model_.skeleton.empty() ? -1 : 0;

    // Build the rigid-body solver from the model's bodies and rig. A model
    // with no rigid bodies leaves it empty, and every physics call is a no-op.
    physics_.build(model_.rigid_bodies, model_.skeleton);
    physics_.reset();

    valid_ = indices_.valid() && vertices_.valid();
    if (!valid_) {
        error = "failed to allocate GPU buffers for the model";
        clear(renderer);
        return false;
    }
    return true;
}

void ModelInstance::clear(Renderer& renderer) {
    for (GpuTexture& texture : textures_) {
        renderer.destroy_texture(texture);
    }
    textures_.clear();
    renderer.destroy_index_buffer(indices_);
    renderer.destroy_dynamic_mesh(vertices_);
    model_ = Model{};
    pose_ = Pose{};
    transform_ = Transform{};
    physics_.clear();
    clip_ = AnimationClip{};
    has_clip_ = false;
    source_path_.clear();
    motion_path_.clear();
    ik_enabled_ = true;
    selected_bone_ = -1;
    vertex_offsets_.clear();
    bone_offsets_.clear();
    valid_ = false;
}

void ModelInstance::evaluate() {
    // Gather the offsets the active morphs contribute. Vertex offsets move the
    // mesh before skinning; bone offsets are folded in by evaluate_pose_full.
    model_.morphs.accumulate_offsets(model_.mesh.vertices.size(),
                                     model_.skeleton.size(), vertex_offsets_,
                                     bone_offsets_);
    evaluate_pose_full(model_.skeleton, pose_, bone_offsets_, ik_enabled_);
}

void ModelInstance::apply_animation(float frame) {
    if (!valid_ || !has_clip_) {
        return;
    }
    apply_clip(model_.skeleton, clip_, frame, pose_);

    // Morph tracks in the clip drive the matching morph weights. A morph with
    // no track keeps whatever the user set, so a slider is not stomped.
    MorphSet& morphs = model_.morphs;
    for (const ScalarTrack& track : clip_.scalar_tracks) {
        const int index = morphs.find(track.name);
        if (index >= 0) {
            morphs.set_weight(index, track.sample(frame));
        }
    }
    evaluate();
}

void ModelInstance::update(Renderer& renderer, float dt) {
    if (!valid_) {
        return;
    }
    evaluate();
    // Advance the rigid-body simulation *after* the keyframe/IK pose is
    // evaluated but before skinning, so a hair or skirt strand is skinned from
    // its simulated position. A disabled or empty solver leaves the pose
    // exactly as evaluated.
    physics_.step(model_.skeleton, pose_, dt);
    const MeshData skinned = skin_mesh(model_.mesh, pose_, vertex_offsets_);
    renderer.upload_vertices(vertices_, skinned.vertices);
}

void ModelInstance::reset_physics() {
    if (!valid_ || physics_.empty()) {
        return;
    }
    physics_.reset();
    evaluate();
}

const GpuTexture& ModelInstance::texture_for(const Material& material) const {
    static const GpuTexture kNone;
    if (material.texture < 0 ||
        material.texture >= static_cast<int>(textures_.size())) {
        return kNone;
    }
    return textures_[static_cast<std::size_t>(material.texture)];
}

void ModelInstance::draw(Renderer& renderer, const mat4& view_proj) const {
    if (!valid_) {
        return;
    }
    const mat4 model = model_matrix();
    for (const Material& material : model_.materials) {
        if (material.index_count == 0) {
            continue;
        }
        renderer.draw_skinned(vertices_, indices_, material.index_offset,
                              material.index_count, texture_for(material),
                              material.diffuse, model, view_proj,
                              material.double_sided);
    }
}

void ModelInstance::set_bone_global(int bone, const mat4& new_global) {
    if (bone < 0 || bone >= static_cast<int>(model_.skeleton.size())) {
        return;
    }
    const Bone& b = model_.skeleton.bone(bone);
    const mat4 parent_global =
        (b.parent >= 0) ? pose_.global[static_cast<std::size_t>(b.parent)]
                        : mat4(1.0f);

    // pose.global = parent_global * rest_local * delta, so invert the two
    // known factors to recover the delta the rig stores.
    const mat4 local = glm::inverse(parent_global) * new_global;
    const mat4 delta = glm::inverse(b.rest_local) * local;
    pose_.delta[static_cast<std::size_t>(bone)] = Transform::from_matrix(delta);

    // Children read this bone's global, so the whole chain must be refreshed
    // before the next skinning pass.
    evaluate();
}

bool ModelInstance::set_morph_weight(const std::string& name, float weight) {
    if (!valid_) {
        return false;
    }
    const int index = model_.morphs.find(name);
    if (index < 0) {
        return false;
    }
    model_.morphs.set_weight(index, weight);
    evaluate();
    return true;
}

bool ModelInstance::set_bone_delta(const std::string& name,
                                   const Transform& delta) {
    if (!valid_) {
        return false;
    }
    const std::vector<Bone>& bones = model_.skeleton.bones();
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name != name) {
            continue;
        }
        pose_.delta[i] = delta;
        evaluate();
        return true;
    }
    return false;
}

}  // namespace ms
