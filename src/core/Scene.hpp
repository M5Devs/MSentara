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

#include "core/Entity.hpp"

namespace ms {

// A flat, id-addressed container of entities. A flat store (rather than a
// pointer-based tree) keeps the data copyable and easy to serialise, while
// still expressing hierarchy through Entity::parent.
class Scene {
public:
    Scene();

    // Creates a new entity and returns its id. The id is never reused.
    EntityId create_entity(const std::string& name,
                           EntityType type = EntityType::Empty,
                           EntityId parent = kInvalidEntity);

    // Destroys an entity and all of its descendants.
    bool destroy_entity(EntityId id);

    Entity* find(EntityId id);
    const Entity* find(EntityId id) const;

    const std::vector<Entity>& entities() const { return entities_; }
    std::size_t size() const { return entities_.size(); }

    // World matrix: parent chain composed with this entity's local transform.
    mat4 world_matrix(EntityId id) const;

    // Selection helpers. At most one entity is selected at a time for now.
    EntityId selected() const { return selected_; }
    void select(EntityId id);
    Entity* selected_entity();

    void clear();

private:
    std::vector<Entity> entities_;
    EntityId next_id_ = 1;
    EntityId selected_ = kInvalidEntity;
};

}  // namespace ms
