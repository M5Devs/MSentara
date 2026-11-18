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

#include "TestCommon.hpp"

#include "core/Mesh.hpp"

using namespace ms;
using namespace ms::test;

namespace {

void test_cube_geometry() {
    const MeshData cube = make_unit_cube();
    // Six faces, four vertices each, two triangles per face.
    MS_CHECK(cube.vertices.size() == 24);
    MS_CHECK(cube.indices.size() == 36);
}

void test_cube_bounds_and_winding() {
    const MeshData cube = make_unit_cube();
    for (const Vertex& v : cube.vertices) {
        MS_CHECK(std::fabs(v.position.x) <= 0.5f + 1e-5f);
        MS_CHECK(std::fabs(v.position.y) <= 0.5f + 1e-5f);
        MS_CHECK(std::fabs(v.position.z) <= 0.5f + 1e-5f);
    }

    // Every triangle must wind counter-clockwise when viewed from outside,
    // i.e. its geometric normal must point the same way as the vertex normal.
    for (std::size_t i = 0; i + 2 < cube.indices.size(); i += 3) {
        const vec3& a = cube.vertices[cube.indices[i + 0]].position;
        const vec3& b = cube.vertices[cube.indices[i + 1]].position;
        const vec3& c = cube.vertices[cube.indices[i + 2]].position;
        const vec3 face_normal = glm::normalize(glm::cross(b - a, c - a));
        const vec3 vertex_normal = cube.vertices[cube.indices[i]].normal;
        MS_CHECK(glm::dot(face_normal, vertex_normal) > 0.99f);
    }
}

void test_cube_vertices_match_normals() {
    const MeshData cube = make_unit_cube();
    for (const Vertex& v : cube.vertices) {
        // Each face's vertices lie on the plane defined by that face normal.
        MS_CHECK(nearly(glm::dot(v.position, v.normal), 0.5f, 1e-5f));
    }
}

void test_grid_geometry() {
    const MeshData grid = make_grid(4, 1.0f);
    // (2*4+1) lines per axis, two vertices each; every line is one index pair.
    const std::size_t lines = 9 * 2;
    MS_CHECK(grid.vertices.size() == lines * 2);
    MS_CHECK(grid.indices.size() == lines * 2);
    for (const Vertex& v : grid.vertices) {
        MS_CHECK(nearly(v.position.y, 0.0f));
        MS_CHECK(nearly(v.normal, vec3(0.0f, 1.0f, 0.0f)));
    }
}

void test_grid_extent() {
    const MeshData grid = make_grid(3, 2.0f);
    float max_coord = 0.0f;
    for (const Vertex& v : grid.vertices) {
        max_coord = std::max(max_coord, std::max(std::fabs(v.position.x),
                                                 std::fabs(v.position.z)));
    }
    MS_CHECK(nearly(max_coord, 6.0f));
}

}  // namespace

int main() {
    test_cube_geometry();
    test_cube_bounds_and_winding();
    test_cube_vertices_match_normals();
    test_grid_geometry();
    test_grid_extent();
    return ms::test::summary("mesh");
}
