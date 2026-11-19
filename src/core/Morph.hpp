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
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/Math.hpp"

namespace ms {

// ---------------------------------------------------------------------------
// Morph targets (a.k.a. blend shapes / shape keys)
//
// A morph is a named set of additive offsets that a weight in [0, 1] scales in.
// The three offset kinds PMX defines and this project evaluates are:
//
//   Vertex  - move individual vertices, before skinning. The facial-expression
//             workhorse.
//   Bone    - add a translation and rotation to a bone, applied at the
//             keyframe stage so IK and append/grant see the morphed pose.
//   Group   - reference other morphs by weight, so one slider can drive a
//             whole expression. Groups are flattened to leaf weights at load.
//
// UV, material, flip and impulse morphs exist in PMX 2.1 but are not evaluated
// yet; the loader advances past them so a real file still parses completely.
// The structure stays format-neutral so a future 2D rig can reuse it.
// ---------------------------------------------------------------------------

struct VertexMorphOffset {
    std::uint32_t vertex = 0;
    vec3 offset{0.0f, 0.0f, 0.0f};
};

struct BoneMorphOffset {
    std::uint32_t bone = 0;
    vec3 translation{0.0f, 0.0f, 0.0f};
    // An additive local rotation for the bone. MMD stores a quaternion, so it
    // is kept as one; the caller composes it with the keyframe rotation.
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct MorphTarget {
    enum class Kind { Vertex, Bone, Group };

    // Canonical name: the local (usually Japanese) name for a PMX morph, which
    // is what a VMD morph track matches against.
    std::string name;

    // Universal (English) name when present, else empty. Display-only.
    std::string name_en;

    // Name to show in the UI: English when available, else canonical.
    const std::string& display_name() const {
        return name_en.empty() ? name : name_en;
    }

    Kind kind = Kind::Vertex;

    // Kind == Vertex
    std::vector<VertexMorphOffset> vertex_offsets;
    // Kind == Bone
    std::vector<BoneMorphOffset> bone_offsets;
    // Kind == Group: (referenced morph index, influence)
    std::vector<std::pair<std::uint32_t, float>> group_refs;

    bool empty() const {
        return vertex_offsets.empty() && bone_offsets.empty() && group_refs.empty();
    }
};

// A whole model's morphs plus the live weights that drive them.
class MorphSet {
public:
    // Appends a morph and returns its index.
    int add(MorphTarget target);

    std::size_t size() const { return targets_.size(); }
    bool empty() const { return targets_.empty(); }

    const std::vector<MorphTarget>& targets() const { return targets_; }
    const MorphTarget& target(int index) const {
        return targets_[static_cast<std::size_t>(index)];
    }

    // Index of the morph with this name, or -1. Used to match a VMD morph
    // track to the model's morphs.
    int find(const std::string& name) const;

    // Per-morph weights, all zero initially. `weights()` is what the UI sliders
    // and the VMD scalar tracks write into.
    std::vector<float>& weights() { return weights_; }
    const std::vector<float>& weights() const { return weights_; }

    void reset_weights();
    void set_weight(int index, float weight);

    // Resolves Group morphs to their leaf (Vertex/Bone) morphs and returns the
    // effective weight of every morph, indexed by morph index. A morph that is
    // both a group member and directly driven adds the two contributions, so a
    // group slider and a direct slider compose the way MMD expects.
    std::vector<float> resolved_weights() const;

    // Adds every active morph's offsets into `vertex_offsets` (resized to
    // `vertex_count`) and into `bone_offsets` (resized to `bone_count`). Only
    // the leaf morphs' resolved weights are used. This is the bridge to
    // skinning: the caller adds vertex_offsets to the mesh's positions.
    void accumulate_offsets(std::size_t vertex_count, std::size_t bone_count,
                            std::vector<vec3>& vertex_offsets,
                            std::vector<BoneMorphOffset>& bone_offsets) const;

private:
    std::vector<MorphTarget> targets_;
    std::vector<float> weights_;
};

}  // namespace ms
