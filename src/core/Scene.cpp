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

#include "core/Scene.hpp"

#include <algorithm>

namespace ms {

Scene::Scene() = default;

EntityId Scene::create_entity(const std::string& name, EntityType type,
                              EntityId parent) {
    Entity e;
    e.id = next_id_++;
    e.parent = (find(parent) != nullptr) ? parent : kInvalidEntity;
    e.name = name;
    e.type = type;
    entities_.push_back(std::move(e));
    return entities_.back().id;
}

bool Scene::destroy_entity(EntityId id) {
    if (find(id) == nullptr) {
        return false;
    }

    // Collect the subtree rooted at `id` before mutating the container.
    std::vector<EntityId> doomed{id};
    for (std::size_t i = 0; i < doomed.size(); ++i) {
        for (const Entity& e : entities_) {
            if (e.parent == doomed[i]) {
                doomed.push_back(e.id);
            }
        }
    }

    const auto is_doomed = [&doomed](EntityId candidate) {
        return std::find(doomed.begin(), doomed.end(), candidate) != doomed.end();
    };

    entities_.erase(
        std::remove_if(entities_.begin(), entities_.end(),
                       [&is_doomed](const Entity& e) { return is_doomed(e.id); }),
        entities_.end());

    if (is_doomed(selected_)) {
        selected_ = kInvalidEntity;
    }
    return true;
}

Entity* Scene::find(EntityId id) {
    for (Entity& e : entities_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

const Entity* Scene::find(EntityId id) const {
    for (const Entity& e : entities_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

mat4 Scene::world_matrix(EntityId id) const {
    const Entity* e = find(id);
    if (e == nullptr) {
        return mat4(1.0f);
    }
    if (e->parent == kInvalidEntity) {
        return e->transform.matrix();
    }
    return world_matrix(e->parent) * e->transform.matrix();
}

void Scene::select(EntityId id) {
    selected_ = (find(id) != nullptr) ? id : kInvalidEntity;
    for (Entity& e : entities_) {
        e.selected = (e.id == selected_);
    }
}

Entity* Scene::selected_entity() { return find(selected_); }

void Scene::clear() {
    entities_.clear();
    selected_ = kInvalidEntity;
    next_id_ = 1;
}

}  // namespace ms
