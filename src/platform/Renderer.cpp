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

#include "platform/Renderer.hpp"

#include <algorithm>
#include <cstddef>

#include "platform/SokolConfig.hpp"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "util/sokol_imgui.h"

namespace ms {

namespace {

// Attribute indices are pinned in the shader with layout(location = N); sokol
// resolves them with glGetAttribLocation, so the glsl_name hints are optional.
constexpr int kAttrPosition = 0;
constexpr int kAttrNormal = 1;
constexpr int kAttrColor = 2;

// Two mat4 members. With SG_UNIFORMLAYOUT_NATIVE sokol packs them back to back
// with no padding, so the block maps onto two plain `uniform mat4`s.
struct ShaderParams {
    float mvp[16];
    float model[16];
};

const char* const kVertexShader = R"(
#version 410

uniform mat4 mvp;
uniform mat4 model;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_color;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    // The grid and cube are rigid, so the model matrix rotates normals
    // correctly without a separate normal matrix.
    v_normal = normalize(mat3(model) * normal);
    v_color = color;
}
)";

const char* const kFragmentShader = R"(
#version 410

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_color;

layout(location = 0) out vec4 frag_color;

void main() {
    // Fixed headlight so the cube's faces read differently without a full
    // lighting model.
    vec3 n = normalize(v_normal);
    float lambert = clamp(dot(n, normalize(vec3(0.4, 0.8, 0.6))), 0.0, 1.0);
    vec3 lit = v_color * (0.35 + 0.65 * lambert);
    frag_color = vec4(lit, 1.0);
}
)";

sg_shader_desc make_shader_desc() {
    sg_shader_desc desc = {};
    desc.vertex_func.source = kVertexShader;
    desc.vertex_func.entry = "main";
    desc.fragment_func.source = kFragmentShader;
    desc.fragment_func.entry = "main";

    // GL 4.1 and WebGL2 cannot bind attribute locations by name, so sokol
    // needs the base types to validate the pipeline layout.
    desc.attrs[kAttrPosition].base_type = SG_SHADERATTRBASETYPE_FLOAT;
    desc.attrs[kAttrNormal].base_type = SG_SHADERATTRBASETYPE_FLOAT;
    desc.attrs[kAttrColor].base_type = SG_SHADERATTRBASETYPE_FLOAT;

    desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    desc.uniform_blocks[0].size = sizeof(ShaderParams);
    desc.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_NATIVE;
    desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    desc.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    desc.uniform_blocks[0].glsl_uniforms[1].type = SG_UNIFORMTYPE_MAT4;
    desc.uniform_blocks[0].glsl_uniforms[1].array_count = 1;
    desc.uniform_blocks[0].glsl_uniforms[1].glsl_name = "model";
    return desc;
}

sg_pipeline make_pipeline(sg_shader shader, sg_primitive_type primitive,
                          bool depth_test) {
    sg_pipeline_desc desc = {};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(Vertex);
    desc.layout.attrs[kAttrPosition].format = SG_VERTEXFORMAT_FLOAT3;
    desc.layout.attrs[kAttrPosition].offset = offsetof(Vertex, position);
    desc.layout.attrs[kAttrNormal].format = SG_VERTEXFORMAT_FLOAT3;
    desc.layout.attrs[kAttrNormal].offset = offsetof(Vertex, normal);
    desc.layout.attrs[kAttrColor].format = SG_VERTEXFORMAT_FLOAT3;
    desc.layout.attrs[kAttrColor].offset = offsetof(Vertex, color);

    desc.primitive_type = primitive;
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.cull_mode = (primitive == SG_PRIMITIVETYPE_TRIANGLES)
                         ? SG_CULLMODE_BACK
                         : SG_CULLMODE_NONE;
    desc.face_winding = SG_FACEWINDING_CCW;
    desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
    desc.depth.compare =
        depth_test ? SG_COMPAREFUNC_LESS_EQUAL : SG_COMPAREFUNC_ALWAYS;
    desc.depth.write_enabled = depth_test;
    // The viewport renders into a single-sampled offscreen target, so the
    // pipeline must not request the swapchain's MSAA count. The UI is drawn
    // by sokol_imgui with its own pipelines at sapp_sample_count().
    desc.sample_count = 1;
    desc.label = "ms-pipeline";
    return sg_make_pipeline(&desc);
}

void submit_matrices(const mat4& mvp, const mat4& model) {
    ShaderParams params{};
    std::copy_n(glm::value_ptr(mvp), 16, params.mvp);
    std::copy_n(glm::value_ptr(model), 16, params.model);
    const sg_range range = SG_RANGE(params);
    sg_apply_uniforms(0, &range);
}

sg_view make_color_attachment_view(std::uint32_t image_id) {
    sg_view_desc desc = {};
    desc.color_attachment.image = sg_image{image_id};
    return sg_make_view(&desc);
}

sg_view make_depth_attachment_view(std::uint32_t image_id) {
    sg_view_desc desc = {};
    desc.depth_stencil_attachment.image = sg_image{image_id};
    return sg_make_view(&desc);
}

sg_view make_texture_view(std::uint32_t image_id) {
    sg_view_desc desc = {};
    desc.texture.image = sg_image{image_id};
    return sg_make_view(&desc);
}

}  // namespace

void Renderer::setup() {
    if (setup_) {
        return;
    }

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);

    sg_shader_desc shader_desc = make_shader_desc();
    shader_ = sg_make_shader(&shader_desc).id;
    triangle_pipeline_.id =
        make_pipeline(sg_shader{shader_}, SG_PRIMITIVETYPE_TRIANGLES, true).id;
    line_pipeline_.id =
        make_pipeline(sg_shader{shader_}, SG_PRIMITIVETYPE_LINES, true).id;

    setup_ = true;
}

void Renderer::shutdown() {
    if (!setup_) {
        return;
    }
    if (triangle_pipeline_.valid()) {
        sg_destroy_pipeline(sg_pipeline{triangle_pipeline_.id});
    }
    if (line_pipeline_.valid()) {
        sg_destroy_pipeline(sg_pipeline{line_pipeline_.id});
    }
    if (shader_ != 0) {
        sg_destroy_shader(sg_shader{shader_});
    }
    sg_shutdown();
    setup_ = false;
}

GpuMesh Renderer::create_mesh(const MeshData& mesh) {
    GpuMesh gpu;

    sg_buffer_desc vdesc = {};
    vdesc.data =
        sg_range{mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex)};
    vdesc.label = "ms-vbuf";
    gpu.vbuf = sg_make_buffer(&vdesc).id;

    sg_buffer_desc idesc = {};
    idesc.usage.index_buffer = true;
    idesc.data = sg_range{mesh.indices.data(),
                          mesh.indices.size() * sizeof(std::uint32_t)};
    idesc.label = "ms-ibuf";
    gpu.ibuf = sg_make_buffer(&idesc).id;

    gpu.index_count = static_cast<int>(mesh.indices.size());
    return gpu;
}

void Renderer::destroy_mesh(GpuMesh& mesh) {
    if (mesh.vbuf != 0) {
        sg_destroy_buffer(sg_buffer{mesh.vbuf});
    }
    if (mesh.ibuf != 0) {
        sg_destroy_buffer(sg_buffer{mesh.ibuf});
    }
    mesh = GpuMesh{};
}

void Renderer::resize_render_target(RenderTarget& rt, int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (rt.valid() && rt.width == width && rt.height == height) {
        return;
    }
    destroy_render_target(rt);

    sg_image_desc color_desc = {};
    color_desc.width = width;
    color_desc.height = height;
    color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    // Render-target images otherwise inherit sg_desc.environment.defaults
    // .sample_count (the swapchain's MSAA count); this target is single
    // sampled so the UI can sample it directly and the pipelines match.
    color_desc.sample_count = 1;
    color_desc.usage.color_attachment = true;
    color_desc.label = "ms-vp-color";
    rt.color_image = sg_make_image(&color_desc).id;

    sg_image_desc depth_desc = {};
    depth_desc.width = width;
    depth_desc.height = height;
    depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH;
    depth_desc.sample_count = 1;
    depth_desc.usage.depth_stencil_attachment = true;
    depth_desc.label = "ms-vp-depth";
    rt.depth_image = sg_make_image(&depth_desc).id;

    rt.color_attachment_view = make_color_attachment_view(rt.color_image).id;
    rt.depth_attachment_view = make_depth_attachment_view(rt.depth_image).id;
    rt.texture_view = make_texture_view(rt.color_image).id;

    rt.width = width;
    rt.height = height;
    // The UI layer samples the colour image through this ImGui texture handle.
    rt.imgui_texture_id = simgui_imtextureid(sg_view{rt.texture_view});
}

void Renderer::destroy_render_target(RenderTarget& rt) {
    if (rt.color_attachment_view != 0) {
        sg_destroy_view(sg_view{rt.color_attachment_view});
    }
    if (rt.depth_attachment_view != 0) {
        sg_destroy_view(sg_view{rt.depth_attachment_view});
    }
    if (rt.texture_view != 0) {
        sg_destroy_view(sg_view{rt.texture_view});
    }
    if (rt.color_image != 0) {
        sg_destroy_image(sg_image{rt.color_image});
    }
    if (rt.depth_image != 0) {
        sg_destroy_image(sg_image{rt.depth_image});
    }
    rt = RenderTarget{};
}

void Renderer::begin_offscreen_pass(const RenderTarget& rt,
                                    const vec3& clear_color) {
    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {clear_color.r, clear_color.g,
                                         clear_color.b, 1.0f};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    pass.attachments.colors[0] = sg_view{rt.color_attachment_view};
    pass.attachments.depth_stencil = sg_view{rt.depth_attachment_view};
    sg_begin_pass(&pass);
}

void Renderer::begin_swapchain_pass(const vec3& clear_color) {
    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {clear_color.r, clear_color.g,
                                         clear_color.b, 1.0f};
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
}

void Renderer::end_pass() { sg_end_pass(); }

void Renderer::draw_triangles(const GpuMesh& mesh, const mat4& model,
                              const mat4& view_proj) {
    if (!mesh.valid() || !triangle_pipeline_.valid()) {
        return;
    }
    sg_apply_pipeline(sg_pipeline{triangle_pipeline_.id});
    sg_bindings bindings = {};
    bindings.vertex_buffers[0] = sg_buffer{mesh.vbuf};
    bindings.index_buffer = sg_buffer{mesh.ibuf};
    sg_apply_bindings(&bindings);
    submit_matrices(view_proj * model, model);
    sg_draw(0, mesh.index_count, 1);
}

void Renderer::draw_lines(const GpuMesh& mesh, const mat4& model,
                          const mat4& view_proj) {
    if (!mesh.valid() || !line_pipeline_.valid()) {
        return;
    }
    sg_apply_pipeline(sg_pipeline{line_pipeline_.id});
    sg_bindings bindings = {};
    bindings.vertex_buffers[0] = sg_buffer{mesh.vbuf};
    bindings.index_buffer = sg_buffer{mesh.ibuf};
    sg_apply_bindings(&bindings);
    submit_matrices(view_proj * model, model);
    sg_draw(0, mesh.index_count, 1);
}

}  // namespace ms
