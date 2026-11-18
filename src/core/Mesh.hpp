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
#include <vector>

#include "core/Math.hpp"

namespace ms {

// CPU-side geometry description. Contains no graphics-API handles, so it can
// be produced and tested entirely without a GPU. The platform layer uploads
// this into sokol_gfx buffers.
//
// A vertex carries up to four skinning influences. Four is the ceiling every
// practical format agrees on (PMX BDEF4, glTF JOINTS_0), and it keeps the
// vertex small enough to re-upload every frame when skinning on the CPU.
inline constexpr int kMaxSkinInfluences = 4;

struct Vertex {
    vec3 position{0.0f, 0.0f, 0.0f};
    vec3 normal{0.0f, 1.0f, 0.0f};
    vec3 color{1.0f, 1.0f, 1.0f};
    vec2 uv{0.0f, 0.0f};

    // Bone indices into the model's skeleton, paired with the matching weight.
    // A vertex whose weights sum to zero is rigid: it is transformed by the
    // model matrix alone, which is how the unrigged cube and grid work.
    std::uint32_t bone_indices[kMaxSkinInfluences]{0, 0, 0, 0};
    float bone_weights[kMaxSkinInfluences]{0.0f, 0.0f, 0.0f, 0.0f};

    void set_influence(int slot, std::uint32_t bone, float weight) {
        if (slot < 0 || slot >= kMaxSkinInfluences) {
            return;
        }
        bone_indices[slot] = bone;
        bone_weights[slot] = weight;
    }

    float total_weight() const {
        float sum = 0.0f;
        for (float w : bone_weights) {
            sum += w;
        }
        return sum;
    }
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Axis-aligned bounds of a mesh, plus the bounding sphere the camera frames.
struct Bounds {
    vec3 min{0.0f, 0.0f, 0.0f};
    vec3 max{0.0f, 0.0f, 0.0f};
    vec3 center{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;

    bool valid() const { return radius > 0.0f; }
};

Bounds compute_bounds(const MeshData& mesh);

// Unit cube centred on the origin, with per-face colours.
MeshData make_unit_cube();

// Floor grid on the XZ plane: `half_extent` cells in each direction with
// `cell_size` spacing, centred on the origin.
MeshData make_grid(int half_extent = 10, float cell_size = 1.0f);

}  // namespace ms
