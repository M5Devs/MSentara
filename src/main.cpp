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

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/Mesh.hpp"
#include "platform/Renderer.hpp"
#include "platform/SokolConfig.hpp"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include "ui/Editor.hpp"
#include "ui/Theme.hpp"
#include "util/sokol_imgui.h"

namespace {

using namespace ms;

// Background colours, matching the Blender dark theme.
constexpr vec3 kViewportBg{0.20f, 0.20f, 0.22f};
constexpr vec3 kWindowBg{0.17f, 0.17f, 0.17f};

Renderer g_renderer;
Editor g_editor;
GpuMesh g_cube;
GpuMesh g_grid;
RenderTarget g_viewport;
int g_frame_count = 0;

// Optional frame limit, used by the headless smoke test to exit on its own.
// Unset (or 0) means "run until the user quits".
int g_frame_limit = 0;

// Offscreen target size, recomputed each frame from the viewport panel.
int g_target_w = 0;
int g_target_h = 0;

void apply_camera_navigation();

void init_cb() {
    g_renderer.setup();
    simgui_desc_t simgui_desc = {};
    simgui_desc.logger.func = slog_func;
    simgui_setup(&simgui_desc);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Multi-viewport is intentionally off: sokol_app manages a single window,
    // and enabling it without a matching platform backend would break.
    io.IniFilename = nullptr;

    apply_blender_dark_theme();
    g_editor.setup();

    g_cube = g_renderer.create_mesh(make_unit_cube());
    g_grid = g_renderer.create_mesh(make_grid(10, 1.0f));

    if (const char* limit = std::getenv("MSENTARA_FRAME_LIMIT")) {
        g_frame_limit = std::atoi(limit);
    }
}

void frame_cb() {
    ++g_frame_count;
    if (g_frame_limit > 0 && g_frame_count >= g_frame_limit) {
        sapp_request_quit();
    }

    // Resize the offscreen target to match the viewport panel. The panel size
    // is only known after the UI has run once, so on the first frame we fall
    // back to the full window.
    float vx = 0.0f, vy = 0.0f, vw = 0.0f, vh = 0.0f;
    g_editor.viewport_rect(vx, vy, vw, vh);
    if (vw < 1.0f || vh < 1.0f) {
        vw = static_cast<float>(sapp_width());
        vh = static_cast<float>(sapp_height());
    }

    const int want_w = static_cast<int>(vw);
    const int want_h = static_cast<int>(vh);
    if (want_w != g_target_w || want_h != g_target_h) {
        g_target_w = want_w;
        g_target_h = want_h;
        g_renderer.resize_render_target(g_viewport, want_w, want_h);
    }

    apply_camera_navigation();
    g_editor.timeline().advance(static_cast<float>(sapp_frame_duration()));

    // 1) Render the 3D scene into the offscreen target.
    if (g_viewport.valid()) {
        const float aspect =
            (g_viewport.height > 0)
                ? static_cast<float>(g_viewport.width) / static_cast<float>(g_viewport.height)
                : 1.0f;
        const mat4 view_proj = g_editor.camera().view_projection(aspect);

        g_renderer.begin_offscreen_pass(g_viewport, kViewportBg);
        g_renderer.draw_lines(g_grid, mat4(1.0f), view_proj);

        // Draw every visible mesh entity in the scene.
        for (const Entity& e : g_editor.scene().entities()) {
            if (e.type != EntityType::Mesh || !e.visible) {
                continue;
            }
            const mat4 model = g_editor.scene().world_matrix(e.id);
            g_renderer.draw_triangles(g_cube, model, view_proj);
        }
        g_renderer.end_pass();
    }

    // 2) Build the UI (which samples the offscreen texture).
    simgui_frame_desc_t frame_desc = {};
    frame_desc.width = sapp_width();
    frame_desc.height = sapp_height();
    frame_desc.delta_time = sapp_frame_duration();
    frame_desc.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&frame_desc);
    g_editor.draw(g_viewport);
    simgui_flush();

    // 3) Composite the UI into the swapchain.
    g_renderer.begin_swapchain_pass(kWindowBg);
    simgui_draw();
    g_renderer.end_pass();

    // sokol_gfx defers resource creation and buffer/image updates to
    // sg_commit(), so this call is what actually uploads the frame.
    sg_commit();
}

void cleanup_cb() {
    g_renderer.destroy_mesh(g_cube);
    g_renderer.destroy_mesh(g_grid);
    g_renderer.destroy_render_target(g_viewport);
    simgui_shutdown();
    g_renderer.shutdown();
}

// Blender's viewport navigation: middle-drag orbits, shift+middle-drag pans,
// ctrl+middle-drag (or the wheel) zooms. The gizmo takes priority, so
// navigation is suppressed while a manipulator handle is hovered or dragged.
void apply_camera_navigation() {
    if (!g_editor.viewport_hovered()) {
        return;
    }
    // The viewport is an ImGui window, so WantCaptureMouse is always set while
    // the cursor is inside it; gating on it here would disable navigation
    // entirely. The gizmo check below is what actually arbitrates input.
    if (ImGuizmo::IsUsing() || ImGuizmo::IsOver()) {
        return;
    }

    Camera& cam = g_editor.camera();
    ImGuiIO& io = ImGui::GetIO();
    const bool middle = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    if (middle) {
        const ImVec2 d = io.MouseDelta;
        if (io.KeyShift) {
            cam.pan(d.x, d.y, g_target_h > 0 ? static_cast<float>(g_target_h) : 1.0f);
        } else if (io.KeyCtrl) {
            cam.zoom(-d.y * 0.02f);
        } else if (d.x != 0.0f || d.y != 0.0f) {
            cam.orbit(d.x, d.y, 0.3f);
        }
    }
    if (io.MouseWheel != 0.0f) {
        cam.zoom(io.MouseWheel);
    }
}

void event_cb(const sapp_event* ev) {
    if (simgui_handle_event(ev)) {
        return;  // the UI consumed the event
    }

    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
        Editor& editor = g_editor;
        switch (ev->key_code) {
            // Gizmo mode: Blender uses G/R/S, many DCCs use W/E/R. Support both.
            case SAPP_KEYCODE_G:
            case SAPP_KEYCODE_W:
                editor.set_gizmo_mode(GizmoMode::Translate);
                break;
            case SAPP_KEYCODE_R:
                // R is Rotate in Blender's G/R/S set but Scale in the DCC
                // W/E/R set. Blender semantics win; Shift+R gives DCC users
                // their Scale.
                editor.set_gizmo_mode(ev->modifiers & SAPP_MODIFIER_SHIFT
                                          ? GizmoMode::Scale
                                          : GizmoMode::Rotate);
                break;
            case SAPP_KEYCODE_S:
                editor.set_gizmo_mode(GizmoMode::Scale);
                break;
            case SAPP_KEYCODE_E:
                editor.set_gizmo_mode(GizmoMode::Rotate);
                break;
            // Blender numpad view presets.
            case SAPP_KEYCODE_KP_1:
                editor.camera().set_view_front();
                break;
            case SAPP_KEYCODE_KP_3:
                editor.camera().set_view_right();
                break;
            case SAPP_KEYCODE_KP_7:
                editor.camera().set_view_top();
                break;
            case SAPP_KEYCODE_KP_5:
                editor.camera().orthographic = !editor.camera().orthographic;
                break;
            case SAPP_KEYCODE_PERIOD:
                editor.camera().frame(vec3(0.0f), 1.0f);
                break;
            case SAPP_KEYCODE_SPACE:
                editor.timeline().playing = !editor.timeline().playing;
                break;
            default:
                break;
        }
    }
}

}  // namespace

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    sapp_desc desc = {};
    desc.init_cb = init_cb;
    desc.frame_cb = frame_cb;
    desc.cleanup_cb = cleanup_cb;
    desc.event_cb = event_cb;
    desc.width = 1600;
    desc.height = 900;
    desc.window_title = "MSentara";
#ifndef MSENTARA_SAMPLE_COUNT
#define MSENTARA_SAMPLE_COUNT 4
#endif
    desc.sample_count = MSENTARA_SAMPLE_COUNT;
    desc.logger.func = slog_func;
    return desc;
}
