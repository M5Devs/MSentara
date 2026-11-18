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

#include "core/Scene.hpp"

using namespace ms;
using namespace ms::test;

namespace {

void test_create_and_find() {
    Scene s;
    const EntityId a = s.create_entity("Cube", EntityType::Mesh);
    const EntityId b = s.create_entity("Camera", EntityType::Camera);
    MS_CHECK(a != kInvalidEntity);
    MS_CHECK(b != kInvalidEntity);
    MS_CHECK(a != b);
    MS_CHECK(s.size() == 2);
    MS_CHECK(s.find(a) != nullptr);
    MS_CHECK(std::string(s.find(b)->name) == "Camera");
    MS_CHECK(s.find(9999) == nullptr);
}

void test_hierarchy_world_matrix() {
    Scene s;
    const EntityId parent = s.create_entity("Parent");
    const EntityId child = s.create_entity("Child", EntityType::Mesh, parent);

    s.find(parent)->transform.position = vec3(10.0f, 0.0f, 0.0f);
    s.find(child)->transform.position = vec3(0.0f, 5.0f, 0.0f);

    const mat4 world = s.world_matrix(child);
    MS_CHECK_NEAR(vec3(world[3]), vec3(10.0f, 5.0f, 0.0f));
}

void test_parent_rotation_composes() {
    Scene s;
    const EntityId parent = s.create_entity("Parent");
    const EntityId child = s.create_entity("Child", EntityType::Empty, parent);

    // Parent rotated +90° about X: a child offset +Y lands at +Z in world.
    s.find(parent)->transform.rotation = vec3(90.0f, 0.0f, 0.0f);
    s.find(child)->transform.position = vec3(0.0f, 1.0f, 0.0f);

    const mat4 world = s.world_matrix(child);
    MS_CHECK_NEAR(vec3(world[3]), vec3(0.0f, 0.0f, 1.0f));
}

void test_destroy_cascades() {
    Scene s;
    const EntityId root = s.create_entity("Root");
    const EntityId mid = s.create_entity("Mid", EntityType::Empty, root);
    const EntityId leaf = s.create_entity("Leaf", EntityType::Empty, mid);
    const EntityId other = s.create_entity("Other");

    MS_CHECK(s.destroy_entity(root));
    MS_CHECK(s.size() == 1);
    MS_CHECK(s.find(root) == nullptr);
    MS_CHECK(s.find(mid) == nullptr);
    MS_CHECK(s.find(leaf) == nullptr);
    MS_CHECK(s.find(other) != nullptr);
    MS_CHECK(!s.destroy_entity(root));
}

void test_parenting_to_missing_is_root() {
    Scene s;
    const EntityId e = s.create_entity("Orphan", EntityType::Empty, 4242);
    MS_CHECK(s.find(e)->parent == kInvalidEntity);
    MS_CHECK_NEAR(s.world_matrix(e), mat4(1.0f));
}

void test_selection() {
    Scene s;
    const EntityId a = s.create_entity("A");
    const EntityId b = s.create_entity("B");

    s.select(a);
    MS_CHECK(s.selected() == a);
    MS_CHECK(s.find(a)->selected);
    MS_CHECK(!s.find(b)->selected);

    s.select(b);
    MS_CHECK(s.selected() == b);
    MS_CHECK(!s.find(a)->selected);
    MS_CHECK(s.find(b)->selected);

    // Selecting a nonexistent entity clears the selection.
    s.select(777);
    MS_CHECK(s.selected() == kInvalidEntity);
    MS_CHECK(s.selected_entity() == nullptr);
}

void test_destroy_selected_clears_selection() {
    Scene s;
    const EntityId a = s.create_entity("A");
    s.select(a);
    s.destroy_entity(a);
    MS_CHECK(s.selected() == kInvalidEntity);
}

void test_clear_resets_ids() {
    Scene s;
    s.create_entity("A");
    s.clear();
    MS_CHECK(s.size() == 0);
    const EntityId first = s.create_entity("B");
    MS_CHECK(first == 1);
}

}  // namespace

int main() {
    test_create_and_find();
    test_hierarchy_world_matrix();
    test_parent_rotation_composes();
    test_destroy_cascades();
    test_parenting_to_missing_is_root();
    test_selection();
    test_destroy_selected_clears_selection();
    test_clear_resets_ids();
    return ms::test::summary("scene");
}
