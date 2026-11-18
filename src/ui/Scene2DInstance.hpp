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
#include <vector>

#include "core/Animation.hpp"
#include "core/Layer2D.hpp"
#include "core/Mesh.hpp"
#include "core/Skeleton.hpp"
#include "platform/Renderer.hpp"
#include "platform/RenderTypes.hpp"

namespace ms {

// A 2D cutout scene together with the GPU textures its layers sample. This is
// the 2D counterpart of ModelInstance: core describes the layers (no graphics
// handles), and this class decodes each layer's image and uploads it.
//
// Textures are stored index-aligned with Scene2D::layers(), so a layer's image
// is always textures_[layer_index]. Removing a layer therefore also has to
// drop its texture; Scene2D has no remove today, so the two never drift.
class Scene2DInstance {
public:
    Scene2D& scene() { return scene_; }
    const Scene2D& scene() const { return scene_; }

    // The 2D animation clip, keyed by layer name. Authored in the editor (the
    // 2D keying tools) rather than imported, so it lives here next to the
    // scene it animates.
    AnimationClip& clip() { return clip_; }
    const AnimationClip& clip() const { return clip_; }
    bool has_clip() const { return !clip_.empty(); }

    // Appends a layer whose image is decoded from `path`. The layer's `size`
    // is set from the decoded dimensions so the sprite quad matches the image.
    // A missing/undecodable file is not fatal: the layer is still created and
    // the returned id is valid, but it keeps a zero size and so draws nothing
    // (behaving like a group node) until an image is set.
    LayerId add_layer_from_image(const std::string& name, const std::string& path,
                                 Renderer& renderer,
                                 LayerId parent = kInvalidLayer);

    // Appends an empty group layer (no image). Used as a rig root or an
    // organising node.
    LayerId add_group(const std::string& name, LayerId parent = kInvalidLayer);

    // Replaces a layer's image, uploading the new texture and updating `size`.
    // Returns false for an invalid id or an undecodable file (the old image is
    // kept in that case).
    bool set_layer_image(LayerId id, const std::string& path, Renderer& renderer);

    // Draws every visible layer in z-order. `quad` is the shared unit quad and
    // `view_proj` the 2D projection. Must be called inside a pass.
    //
    // When `skeleton` and `pose` are given, a layer bound to a resolvable bone
    // is placed by that bone's posed transform instead of its parent chain.
    void draw(Renderer& renderer, const GpuMesh& quad, const mat4& view_proj,
              const Skeleton* skeleton = nullptr,
              const Pose* pose = nullptr) const;

    // Draws every layer of `ghost_scene` with a single forced `tint` (for onion
    // skinning). `ghost_scene` is expected to be a copy of this scene whose
    // layer tracks have been evaluated at a neighbouring frame, so its layer
    // ids align with the textures here. Layers with nothing to draw are
    // skipped. Must be called inside a pass.
    void draw_ghost_frame(Renderer& renderer, const GpuMesh& quad,
                          const mat4& view_proj, const Scene2D& ghost_scene,
                          const vec4& tint, const Skeleton* skeleton = nullptr,
                          const Pose* pose = nullptr) const;

    // Releases every GPU texture and forgets the scene.
    void clear(Renderer& renderer);

    // The model matrix that scales the unit quad to a layer's pixel size and
    // applies the layer's world transform. Shared by draw() and the 2D gizmo.
    mat4 layer_model_matrix(LayerId id, const Skeleton* skeleton = nullptr,
                            const Pose* pose = nullptr) const;

private:
    Scene2D scene_;
    AnimationClip clip_;
    std::vector<GpuTexture> textures_;
};

}  // namespace ms
