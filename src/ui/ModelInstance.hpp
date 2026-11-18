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

#include <string>
#include <utility>
#include <vector>

#include "core/Animation.hpp"
#include "core/Material.hpp"
#include "core/Mesh.hpp"
#include "core/Skeleton.hpp"
#include "core/Transform.hpp"
#include "platform/Renderer.hpp"
#include "platform/RenderTypes.hpp"

namespace ms {

// A model that has been loaded from disk and uploaded to the GPU, together
// with its live pose. This is the bridge between the format/core layers
// (which know nothing about graphics) and the renderer.
//
// Skinning runs on the CPU every frame and the result is streamed into a
// transient vertex buffer, so a pose edit is visible immediately and the
// pipeline needs no bone-matrix uniform array (which WebGL2 would not allow
// at the size a full rig needs).
class ModelInstance {
public:
    // Loads a PMX file, decodes its textures and uploads everything. On
    // failure `error` explains why and the instance is left invalid.
    bool load(const std::string& path, Renderer& renderer, std::string& error);

    // Releases the GPU resources and forgets the model.
    void clear(Renderer& renderer);

    bool valid() const { return valid_; }
    const std::string& name() const { return model_.name; }

    const Model& model() const { return model_; }
    Model& model() { return model_; }

    const Pose& pose() const { return pose_; }
    Pose& pose() { return pose_; }

    // Live morph weights (index-aligned with model().morphs). The UI sliders
    // and VMD scalar tracks write here; the next update() applies them.
    MorphSet& morphs() { return model_.morphs; }
    const MorphSet& morphs() const { return model_.morphs; }

    // Enables or disables CCD-IK for this model, so the user can inspect the
    // forward-kinematics pose against the solved one.
    bool ik_enabled() const { return ik_enabled_; }
    void set_ik_enabled(bool enabled) { ik_enabled_ = enabled; }

    // The animation clip currently driving this model, if any. Setting one
    // does not evaluate it; call apply_animation() with the frame to play.
    bool has_animation() const { return has_clip_; }
    const AnimationClip& clip() const { return clip_; }
    void set_clip(AnimationClip clip) {
        clip_ = std::move(clip);
        has_clip_ = !clip_.empty();
    }
    void clear_clip() {
        clip_ = AnimationClip{};
        has_clip_ = false;
    }
    const std::string& clip_name() const { return clip_.name; }

    // Writes the clip at `frame` into the pose (additively over the rest pose)
    // and re-evaluates the skeleton. Does nothing when no clip is loaded.
    // Morph scalar tracks in the clip are written into the morph weights.
    void apply_animation(float frame);

    const Transform& transform() const { return transform_; }
    Transform& transform() { return transform_; }
    mat4 model_matrix() const { return transform_.matrix(); }

    int selected_bone() const { return selected_bone_; }
    void select_bone(int bone) { selected_bone_ = bone; }

    const GpuIndexBuffer& indices() const { return indices_; }
    const GpuDynamicMesh& vertices() const { return vertices_; }

    // Re-evaluates the pose, skins the mesh on the CPU and uploads the result
    // for the current frame. Call once per frame, outside any pass.
    void update(Renderer& renderer);

    // Draws every material range. Must be called inside a pass, after update().
    void draw(Renderer& renderer, const mat4& view_proj) const;

    // Rewrites a bone's pose so that its model-space global matrix becomes
    // `new_global`, then re-evaluates. This is how the gizmo writes back a
    // manipulated bone: the gizmo edits a world matrix, and this converts it
    // into the local delta the rig stores.
    void set_bone_global(int bone, const mat4& new_global);

private:
    const GpuTexture& texture_for(const Material& material) const;

    // Recomputes the pose with the full pipeline (keyframes -> bone morphs ->
    // append -> IK) and gathers the vertex morph offsets for skinning.
    void evaluate();

    Model model_;
    Pose pose_;
    Transform transform_;
    AnimationClip clip_;
    bool has_clip_ = false;
    bool ik_enabled_ = true;
    int selected_bone_ = -1;
    bool valid_ = false;

    // Vertex morph offsets and the bone morph offsets for the current frame.
    // Rebuilt by evaluate() and consumed by update().
    std::vector<vec3> vertex_offsets_;
    std::vector<BoneMorphOffset> bone_offsets_;

    GpuIndexBuffer indices_;
    GpuDynamicMesh vertices_;
    std::vector<GpuTexture> textures_;
};

}  // namespace ms
