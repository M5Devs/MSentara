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

#include "core/Math.hpp"
#include "core/Mesh.hpp"

namespace ms {

// Thin, engine-owned wrapper over sokol_gfx. The rest of the program only ever
// sees these types, so swapping the graphics backend never leaks out of
// src/platform.
struct GpuMesh {
    std::uint32_t vbuf = 0;  // sg_buffer id
    std::uint32_t ibuf = 0;  // sg_buffer id
    int index_count = 0;
    bool valid() const { return vbuf != 0 && ibuf != 0; }
};

struct GpuPipeline {
    std::uint32_t id = 0;  // sg_pipeline id
    bool valid() const { return id != 0; }
};

// GPU-side render target used for the 3D viewport. The ImGui panel samples the
// colour texture of this target, so the viewport is a real dockable panel
// rather than a fixed sub-rectangle of the window.
struct RenderTarget {
    std::uint32_t color_image = 0;
    std::uint32_t depth_image = 0;
    std::uint32_t color_attachment_view = 0;
    std::uint32_t depth_attachment_view = 0;
    std::uint32_t texture_view = 0;
    int width = 0;
    int height = 0;
    std::uint64_t imgui_texture_id = 0;

    bool valid() const { return color_image != 0 && texture_view != 0; }
};

}  // namespace ms
