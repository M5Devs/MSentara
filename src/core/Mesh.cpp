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

#include "core/Mesh.hpp"

#include <algorithm>
#include <cstddef>

namespace ms {

namespace {
// One entry per cube face: outward normal and the four corner offsets, in
// counter-clockwise winding when viewed from outside.
struct Face {
    vec3 normal;
    vec3 corners[4];
    vec3 color;
    // Corner UVs, in the same order as `corners`, so the quad maps to the
    // full 0..1 texture square.
    vec2 uvs[4];
};

constexpr float kH = 0.5f;

// Shared per-face UV square; the four corners map to the four texture corners.
const vec2 kQuadUv[4] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};

const Face kFaces[6] = {
    {{0, 0, 1}, {{-kH, -kH, kH}, {kH, -kH, kH}, {kH, kH, kH}, {-kH, kH, kH}}, {0.85f, 0.35f, 0.35f}, {kQuadUv[0], kQuadUv[1], kQuadUv[2], kQuadUv[3]}},
    {{0, 0, -1}, {{kH, -kH, -kH}, {-kH, -kH, -kH}, {-kH, kH, -kH}, {kH, kH, -kH}}, {0.35f, 0.85f, 0.35f}, {kQuadUv[0], kQuadUv[1], kQuadUv[2], kQuadUv[3]}},
    {{1, 0, 0}, {{kH, -kH, kH}, {kH, -kH, -kH}, {kH, kH, -kH}, {kH, kH, kH}}, {0.35f, 0.45f, 0.85f}, {kQuadUv[0], kQuadUv[1], kQuadUv[2], kQuadUv[3]}},
    {{-1, 0, 0}, {{-kH, -kH, -kH}, {-kH, -kH, kH}, {-kH, kH, kH}, {-kH, kH, -kH}}, {0.85f, 0.85f, 0.35f}, {kQuadUv[0], kQuadUv[1], kQuadUv[2], kQuadUv[3]}},
    {{0, 1, 0}, {{-kH, kH, kH}, {kH, kH, kH}, {kH, kH, -kH}, {-kH, kH, -kH}}, {0.85f, 0.35f, 0.85f}, {kQuadUv[0], kQuadUv[1], kQuadUv[2], kQuadUv[3]}},
    {{0, -1, 0}, {{-kH, -kH, -kH}, {kH, -kH, -kH}, {kH, -kH, kH}, {-kH, -kH, kH}}, {0.35f, 0.85f, 0.85f}, {kQuadUv[0], kQuadUv[1], kQuadUv[2], kQuadUv[3]}},
};
}  // namespace

Bounds compute_bounds(const MeshData& mesh) {
    Bounds bounds;
    if (mesh.vertices.empty()) {
        return bounds;
    }

    bounds.min = mesh.vertices.front().position;
    bounds.max = mesh.vertices.front().position;
    for (const Vertex& v : mesh.vertices) {
        bounds.min = glm::min(bounds.min, v.position);
        bounds.max = glm::max(bounds.max, v.position);
    }
    bounds.center = (bounds.min + bounds.max) * 0.5f;

    // The bounding sphere is centred on the box and must reach every vertex,
    // not just the corners, so measure against the actual points.
    float radius_sq = 0.0f;
    for (const Vertex& v : mesh.vertices) {
        const vec3 d = v.position - bounds.center;
        radius_sq = std::max(radius_sq, glm::dot(d, d));
    }
    bounds.radius = std::sqrt(radius_sq);
    return bounds;
}

MeshData make_unit_cube() {
    MeshData mesh;
    for (const Face& face : kFaces) {
        const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i) {
            Vertex v;
            v.position = face.corners[i];
            v.normal = face.normal;
            v.color = face.color;
            v.uv = face.uvs[i];
            mesh.vertices.push_back(v);
        }
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }
    return mesh;
}

MeshData make_grid(int half_extent, float cell_size) {
    MeshData mesh;
    const float extent = static_cast<float>(half_extent) * cell_size;
    const vec3 normal(0.0f, 1.0f, 0.0f);

    const auto add_line = [&](const vec3& a, const vec3& b, const vec3& color) {
        const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const vec3& p : {a, b}) {
            Vertex v;
            v.position = p;
            v.normal = normal;
            v.color = color;
            mesh.vertices.push_back(v);
        }
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
    };

    const vec3 minor(0.22f, 0.22f, 0.24f);
    const vec3 major(0.30f, 0.30f, 0.33f);
    const vec3 axis_x(0.70f, 0.25f, 0.25f);
    const vec3 axis_z(0.25f, 0.40f, 0.75f);

    for (int i = -half_extent; i <= half_extent; ++i) {
        const float o = static_cast<float>(i) * cell_size;
        const vec3 color = (i == 0) ? axis_z : ((i % 5 == 0) ? major : minor);
        add_line(vec3(o, 0.0f, -extent), vec3(o, 0.0f, extent), color);
    }
    for (int i = -half_extent; i <= half_extent; ++i) {
        const float o = static_cast<float>(i) * cell_size;
        const vec3 color = (i == 0) ? axis_x : ((i % 5 == 0) ? major : minor);
        add_line(vec3(-extent, 0.0f, o), vec3(extent, 0.0f, o), color);
    }
    return mesh;
}

}  // namespace ms
