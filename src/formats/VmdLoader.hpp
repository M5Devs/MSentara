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

#include "core/Animation.hpp"

namespace ms {

// Result of a VMD parse. `clip` is only meaningful when `ok` is true.
struct VmdResult {
    bool ok = false;
    AnimationClip clip;
    std::string model_name;  // UTF-8, from the file header
    std::string error;       // human-readable, empty on success
};

// Parses a VMD (Vocaloid Motion Data) file from a memory buffer.
//
// Bone keyframes become bone tracks; morph keyframes become scalar tracks. The
// buffer overload is the primitive: the path overload reads the file and then
// delegates, and the unit tests drive the buffer overload directly with a
// synthetic motion. Nothing here touches the filesystem or the GPU.
//
// VMD is MMD's left-handed Y-up Z-forward space; translations and rotations
// are mirrored into the right-handed space PMX loading produces (flip Z on
// translation, conjugate the rotation by that mirror), so a clip and a model
// agree on handedness.
VmdResult parse_vmd(const std::uint8_t* data, std::size_t size);
VmdResult load_vmd(const std::string& path);

}  // namespace ms
