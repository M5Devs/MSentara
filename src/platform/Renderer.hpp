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

#include "core/Math.hpp"
#include "core/Mesh.hpp"
#include "platform/RenderTypes.hpp"

namespace ms {

// Owns every sokol_gfx resource the viewport needs. One Renderer instance
// lives for the whole application lifetime.
//
// The shaders are hand-written GLSL for the OpenGL / GLES3 backends (the two
// backends Milestone 0 targets). A Metal/D3D11 port would instead feed
// sokol-shdc bytecode into the same sg_shader_desc.
class Renderer {
public:
    // Initialises sokol_gfx. Must be called from sokol_app's init callback,
    // once a GL context exists.
    void setup();

    // Releases all GPU resources. Safe to call more than once.
    void shutdown();

    GpuMesh create_mesh(const MeshData& mesh);
    void destroy_mesh(GpuMesh& mesh);

    // Creates or resizes the offscreen colour+depth target the 3D viewport is
    // rendered into. Must be called outside a render pass.
    void resize_render_target(RenderTarget& rt, int width, int height);
    void destroy_render_target(RenderTarget& rt);

    void begin_offscreen_pass(const RenderTarget& rt, const vec3& clear_color);
    void begin_swapchain_pass(const vec3& clear_color);
    void end_pass();

    // Both draw calls must be made inside an active pass.
    void draw_triangles(const GpuMesh& mesh, const mat4& model,
                        const mat4& view_proj);
    void draw_lines(const GpuMesh& mesh, const mat4& model,
                    const mat4& view_proj);

private:
    bool setup_ = false;
    std::uint32_t shader_ = 0;  // sg_shader id
    GpuPipeline triangle_pipeline_;
    GpuPipeline line_pipeline_;
};

}  // namespace ms
