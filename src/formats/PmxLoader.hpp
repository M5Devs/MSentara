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

#include "core/Material.hpp"

namespace ms {

// Result of a PMX parse. `model` is only meaningful when `ok` is true.
struct PmxResult {
    bool ok = false;
    Model model;
    std::string error;  // human-readable, empty on success
};

// Parses a PMX 2.0 / 2.1 model from a memory buffer.
//
// The buffer overload is the primitive: the path overload reads the file and
// then delegates, and the unit tests drive the buffer overload directly with
// a synthetic model. Nothing here touches the filesystem, the GPU, or an
// image decoder — textures come back as paths for the platform layer.
PmxResult parse_pmx(const std::uint8_t* data, std::size_t size);
PmxResult load_pmx(const std::string& path);

}  // namespace ms
