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
#include "core/Mesh.hpp"
#include "core/Morph.hpp"
#include "core/RigidBody.hpp"
#include "core/Skeleton.hpp"

namespace ms {

// Decoded image, ready to hand to the graphics backend. The platform layer
// owns decoding (stb_image) and uploading; core only describes the pixels.
struct TextureData {
    int width = 0;
    int height = 0;
    int channels = 0;
    // Tightly packed RGBA8, row-major, top-left origin, `width * height * 4`
    // bytes. Decoders normalise every source format to this.
    std::vector<std::uint8_t> pixels;

    bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() ==
                   static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    }
};

// One drawable surface of a model: a contiguous run of indices plus the
// shading parameters that apply to it. Mirrors PMX's material concept but is
// format-neutral.
struct Material {
    std::string name;

    // Linear-space diffuse, alpha included.
    vec4 diffuse{1.0f, 1.0f, 1.0f, 1.0f};
    vec3 specular{0.0f, 0.0f, 0.0f};
    float shininess = 5.0f;
    vec3 ambient{0.5f, 0.5f, 0.5f};

    // Index into Model::textures, or -1 for "untextured".
    int texture = -1;
    bool double_sided = false;
    bool has_edges = false;

    // Half-open range into Model::mesh.indices.
    std::uint32_t index_offset = 0;
    std::uint32_t index_count = 0;
};

// Everything needed to draw a loaded model, with no graphics handles.
struct Model {
    std::string name;
    MeshData mesh;
    std::vector<Material> materials;

    // Texture file names as stored in the source format, relative to the
    // model's directory. The platform layer decodes these into `textures`
    // (core must not depend on an image decoder). `Material::texture` indexes
    // both vectors.
    std::vector<std::string> texture_paths;
    std::vector<TextureData> textures;

    // One skeleton per model keeps the common case simple; a model with no
    // rig leaves this empty and every vertex is drawn rigid.
    Skeleton skeleton;

    // Blend shapes (facial expressions, etc.). The definitions come from the
    // file; the live weights are driven by a VMD scalar track or the UI.
    MorphSet morphs;

    // Optional rigid-body simulation (hair, skirts, accessories) and the
    // joints that constrain it. Empty when the model carries none, which is
    // common for a plain prop or an unrigged mesh.
    RigidBodySet rigid_bodies;

    bool skinned() const { return !skeleton.empty(); }
    bool simulated() const { return !rigid_bodies.empty(); }
};

}  // namespace ms
