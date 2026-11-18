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

#include "core/Transform.hpp"

namespace ms {

using EntityId = std::uint32_t;
inline constexpr EntityId kInvalidEntity = 0;

enum class EntityType : std::uint8_t {
    Empty,
    Mesh,
    Light,
    Camera,
};

// A single node of the scene graph. The transform is stored in local space;
// the world matrix is derived by walking up to the parent.
struct Entity {
    EntityId id = kInvalidEntity;
    EntityId parent = kInvalidEntity;
    std::string name;
    EntityType type = EntityType::Empty;
    Transform transform;

    // Mesh entities only: identifies the geometry the renderer should draw.
    std::string mesh;

    bool visible = true;
    bool selected = false;
};

}  // namespace ms
