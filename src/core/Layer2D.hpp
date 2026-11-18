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

// Forward declarations: a layer may bind to a bone on a skeleton, but the
// core 2D header stays free of the (heavier) skeletal headers by only taking
// pointers to them.
class Skeleton;
struct Pose;

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

// One named sprite variant of a layer: an expression/state ("closed",
// "open") and the image that draws it. A layer's `variants` list lets one
// cutout swap between several images (mouth open/closed, eye states, ...)
// instead of needing a separate layer per state.
struct LayerVariant {
    std::string name;
    std::string image_path;
    // Pixel size of the variant's image, filled in when the image is decoded
    // (so the editor can resize the layer's quad on a swap without a renderer).
    // Not serialised: it is re-derived from the image on load.
    vec2 size{0.0f, 0.0f};
};

// One 2D cutout layer.
//
// `pivot` is the local point the layer rotates and scales about, in the
// layer's own pixel space measured from its top-left corner (0,0). The
// identity of the drawable is `image_path`; a layer with an empty path still
// participates in the hierarchy and draws nothing (useful as a group/root).
struct Layer2D {
    std::string name;
    std::string image_path;  // PNG/JPG on disk; empty for a pure group node

    // Optional sprite variants (expressions/states). Empty for a plain single
    // image layer, which is the M6 behaviour and stays fully backward
    // compatible. When present, `active_variant` selects which one draws, and
    // a stepped VariantTrack can switch it over time.
    std::vector<LayerVariant> variants;
    int active_variant = 0;  // index into `variants`; ignored when empty

    // Optional skeletal binding: the name of a bone on the active skeleton
    // (a 3D rig or a 2D rig) that drives this layer. When set, the layer's
    // world transform is replaced by that bone's model-space transform, so a
    // cutout part follows a posed (and CCD-IK-solved) bone. Empty means the
    // layer is driven by its own transform and its parent chain, as before.
    // Kept as a name (not an index) so it survives a model re-export.
    std::string bound_bone;

    // Where the layer sits relative to its bound bone, in the bone's own local
    // space. Applied after the bone's global transform, so the cutout can be
    // nudged onto the bone; ignored while `bound_bone` is empty or the bone
    // cannot be resolved. Binding captures this so a freshly bound layer does
    // not jump.
    vec2 bind_offset{0.0f, 0.0f};

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

    // The image this layer draws right now: the active variant's path when the
    // layer has variants (an out-of-range `active_variant` falls back to the
    // first), otherwise the plain `image_path`. This is what the renderer
    // loads and draws, so swapping a variant is a texture change, not a
    // transform change.
    const std::string& active_image_path() const;

    // The name of the active variant, or an empty string for a plain layer.
    // Used as the combo's current label.
    const std::string& active_variant_name() const;

    // The pixel size of the image the layer currently draws: the active
    // variant's size when the layer has variants (and that size is known),
    // otherwise the layer's own `size`. Rendering and hit-testing both use
    // this, so an animation-driven variant swap resizes the quad consistently.
    vec2 active_size() const;
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
    //
    // When `skeleton` and `pose` are given and this layer (or an ancestor) is
    // bound to a resolvable bone, that bone's posed model-space transform
    // replaces the parent chain, so a cutout part follows the bone (including
    // any CCD-IK that posed it). Passing nulls, or an unresolvable bone name,
    // falls back to the plain parent chain, so an unbound scene is unchanged.
    mat4 world_matrix(LayerId id, const Skeleton* skeleton = nullptr,
                      const Pose* pose = nullptr) const;

    // World matrix built from each ancestor's pivot_matrix(), so its origin is
    // this layer's pivot. The 2D gizmo manipulates this form: translation then
    // reads as the layer's position and rotation/scale act about the pivot.
    // Bone bindings are honoured exactly as in world_matrix().
    mat4 pivot_world_matrix(LayerId id, const Skeleton* skeleton = nullptr,
                            const Pose* pose = nullptr) const;

    // The world-space pivot position, i.e. where the gizmo should sit and the
    // point the layer rotates about.
    vec2 world_pivot(LayerId id, const Skeleton* skeleton = nullptr,
                     const Pose* pose = nullptr) const;

    // The matrix the layer's pivot matrix multiplies onto: its bound bone's
    // global matrix (plus bind offset) when bound, otherwise its parent's pivot
    // world matrix, otherwise identity. The gizmo inverts this to turn a
    // manipulated world matrix back into the layer's local transform.
    mat4 pivot_base_matrix(LayerId id, const Skeleton* skeleton = nullptr,
                           const Pose* pose = nullptr) const;

    // Layer indices sorted by z_order (ascending), ties broken by insertion
    // order. This is the draw order the renderer walks.
    std::vector<LayerId> draw_order() const;

    // GPU-independent hit-testing. Returns the top-most *visible* layer whose
    // drawn quad contains `world_point`, or kInvalidLayer when none does.
    // Layers are tested from the front of the draw order backwards (the last
    // drawn is the top-most), so a click selects what the user sees on top.
    //
    // The test maps `world_point` into each layer's own quad space through the
    // inverse of its world matrix, which makes it correct for rotated, scaled
    // and parented layers alike. Bone bindings are honoured exactly as in
    // world_matrix(), so a bound cutout is picked where it is drawn.
    LayerId pick_layer(const vec2& world_point,
                       const Skeleton* skeleton = nullptr,
                       const Pose* pose = nullptr) const;

    LayerId selected() const { return selected_; }
    void select(LayerId id);
    Layer2D* selected_layer();

    void clear();

private:
    // The matrix a layer's own local matrix multiplies onto: its bound bone's
    // global matrix (plus bind offset) when it has a resolvable binding,
    // otherwise its parent's world matrix, otherwise identity.
    mat4 world_parent_matrix(LayerId id, const Skeleton* skeleton,
                             const Pose* pose) const;

    // As above, but built from the parent's *pivot* world matrix, for
    // pivot_world_matrix().
    mat4 pivot_parent_matrix(LayerId id, const Skeleton* skeleton,
                             const Pose* pose) const;

    std::vector<Layer2D> layers_;
    LayerId selected_ = kInvalidLayer;
};

}  // namespace ms
