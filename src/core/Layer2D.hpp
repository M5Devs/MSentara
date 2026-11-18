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

#include <cstdint>
#include <string>
#include <vector>

#include "core/Math.hpp"

namespace ms {

// ---------------------------------------------------------------------------
// 2D layers: a cutout-animation rig.
//
// A Scene2D is a flat, id-addressed list of Layer2D nodes. Hierarchy is
// expressed by each layer's `parent` index, exactly like the 3D Scene's
// entities, so the whole thing stays copyable and trivially serialisable.
//
// This is the classic cutout character rig: a torso layer with arm and head
// children, each a separately drawn image that inherits its parent's motion.
// A child's world transform is therefore its parent's world transform composed
// with its own local transform (translate to pivot, rotate/scale about the
// pivot, offset back), which is what `world_matrix()` computes.
//
// The whole type lives in core and depends only on glm, so it can be unit
// tested with no GPU or window.
// ---------------------------------------------------------------------------

// An index into Scene2D::layers(). Kept as an index rather than a pointer so
// the list stays a value type that copies and serialises cleanly.
using LayerId = int;
inline constexpr LayerId kInvalidLayer = -1;

// One 2D cutout layer.
//
// `pivot` is the local point the layer rotates and scales about, in the
// layer's own pixel space measured from its top-left corner (0,0). The
// identity of the drawable is `image_path`; a layer with an empty path still
// participates in the hierarchy and draws nothing (useful as a group/root).
struct Layer2D {
    std::string name;
    std::string image_path;  // PNG/JPG on disk; empty for a pure group node

    // Parent index into the owning Scene2D, or kInvalidLayer for a root.
    LayerId parent = kInvalidLayer;

    // Pivot in the image's own pixel space, measured from its top-left corner.
    vec2 pivot{0.0f, 0.0f};

    // The image's pixel dimensions. The sprite is drawn as a quad of this
    // size (scaled into place), so a layer with an empty image path draws
    // nothing but still participates in the hierarchy as a group node.
    vec2 size{0.0f, 0.0f};

    // Local transform, relative to the parent layer.
    vec2 position{0.0f, 0.0f};
    float rotation = 0.0f;   // degrees, counter-clockwise
    vec2 scale{1.0f, 1.0f};

    // Paint order within the scene: lower z draws first (behind). A stable
    // sort then keeps insertion order for equal z.
    int z_order = 0;

    float opacity = 1.0f;               // [0, 1]
    vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};  // multiplied with the texture
    bool visible = true;

    // Local matrix: translate(position) * rotate * scale * translate(-pivot).
    // The pivot term is what makes a limb rotate about its shoulder rather
    // than about its own corner.
    mat4 local_matrix() const;

    // The local matrix *without* the trailing translate(-pivot): its origin is
    // the pivot itself. The gizmo manipulates this form so translation reads
    // as the layer's position and rotation/scale act about the pivot.
    mat4 pivot_matrix() const;
};

// A flat collection of 2D layers with a single selected layer.
class Scene2D {
public:
    // Appends a layer and returns its index. A `parent` that does not exist
    // is stored as kInvalidLayer (a root), mirroring Scene::create_entity.
    LayerId add_layer(const std::string& name,
                      LayerId parent = kInvalidLayer);

    Layer2D* find(LayerId id);
    const Layer2D* find(LayerId id) const;

    const std::vector<Layer2D>& layers() const { return layers_; }
    std::vector<Layer2D>& layers() { return layers_; }
    std::size_t size() const { return layers_.size(); }
    bool empty() const { return layers_.empty(); }

    // World matrix: the parent chain composed with this layer's local matrix.
    // A malformed parent (out of range) is treated as a root so evaluation
    // never loops or reads out of bounds.
    mat4 world_matrix(LayerId id) const;

    // World matrix built from each ancestor's pivot_matrix(), so its origin is
    // this layer's pivot. The 2D gizmo manipulates this form: translation then
    // reads as the layer's position and rotation/scale act about the pivot.
    mat4 pivot_world_matrix(LayerId id) const;

    // The world-space pivot position, i.e. where the gizmo should sit and the
    // point the layer rotates about.
    vec2 world_pivot(LayerId id) const;

    // Layer indices sorted by z_order (ascending), ties broken by insertion
    // order. This is the draw order the renderer walks.
    std::vector<LayerId> draw_order() const;

    LayerId selected() const { return selected_; }
    void select(LayerId id);
    Layer2D* selected_layer();

    void clear();

private:
    std::vector<Layer2D> layers_;
    LayerId selected_ = kInvalidLayer;
};

}  // namespace ms
