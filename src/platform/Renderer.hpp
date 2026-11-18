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
#include <vector>

#include "core/Math.hpp"
#include "core/Material.hpp"
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

    // Reads the offscreen target's colour image back into tightly packed RGBA8,
    // with rows ordered top-down (the orientation write_png_rgba expects). Must
    // be called inside the pass for `rt`, after its draws, while its
    // framebuffer is still bound. Returns false on the web build (WebGL2 has no
    // synchronous readback) or for an invalid target.
    bool read_color_pixels(const RenderTarget& rt,
                           std::vector<std::uint8_t>& out);

    // Both draw calls must be made inside an active pass.
    void draw_triangles(const GpuMesh& mesh, const mat4& model,
                        const mat4& view_proj);
    void draw_lines(const GpuMesh& mesh, const mat4& model,
                    const mat4& view_proj);

    // Draws one 2D sprite (an alpha-blended textured quad). `mesh` is the
    // shared unit quad from make_unit_quad(); `model` scales it to the layer's
    // size and places it; `view_proj` is Camera::projection_matrix_2d (the 2D
    // view matrix is identity). `tint` already folds in the layer opacity.
    // Must be called inside a pass, in draw order (back to front).
    void draw_sprite(const GpuMesh& mesh, const GpuTexture& texture,
                     const mat4& model, const mat4& view_proj,
                     const vec4& tint);

    // --- Textures ---------------------------------------------------------
    // Uploads decoded RGBA8 pixels. Returns an invalid handle on empty input.
    GpuTexture create_texture(const TextureData& texture);
    void destroy_texture(GpuTexture& texture);

    // The shared 1x1 white texture, created lazily. Materials without their
    // own texture sample this so the shader never needs a branch.
    const GpuTexture& white_texture();

    // --- Skinned (per-frame dynamic) geometry ------------------------------
    // A model's index buffer is uploaded once; its vertices are re-uploaded
    // every frame from the CPU skinning pass.
    GpuIndexBuffer create_index_buffer(const std::vector<std::uint32_t>& indices);
    void destroy_index_buffer(GpuIndexBuffer& buffer);

    GpuDynamicMesh create_dynamic_mesh(int vertex_capacity);
    void destroy_dynamic_mesh(GpuDynamicMesh& mesh);

    // Writes `vertices` into the dynamic buffer for the current frame, growing
    // the buffer if needed. Must be called outside a pass, once per frame,
    // before any draw_skinned call that uses the buffer.
    bool upload_vertices(GpuDynamicMesh& mesh, const std::vector<Vertex>& vertices);

    // Draws one material range with cel shading and an optional texture. Must
    // be called inside a pass, after upload_vertices for this frame.
    void draw_skinned(const GpuDynamicMesh& vertices,
                      const GpuIndexBuffer& indices,
                      std::uint32_t index_offset, std::uint32_t index_count,
                      const GpuTexture& texture, const vec4& diffuse,
                      const mat4& model, const mat4& view_proj,
                      bool double_sided);

private:
    bool setup_ = false;
    std::uint32_t shader_ = 0;  // sg_shader id
    GpuPipeline triangle_pipeline_;
    GpuPipeline line_pipeline_;

    std::uint32_t skinned_shader_ = 0;
    GpuPipeline skinned_pipeline_;
    GpuPipeline skinned_pipeline_double_;

    std::uint32_t sprite_shader_ = 0;
    GpuPipeline sprite_pipeline_;

    GpuTexture white_texture_;
};

}  // namespace ms
