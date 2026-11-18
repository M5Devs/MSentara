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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "core/Animation.hpp"
#include "core/Mesh.hpp"
#include "core/ProjectIO.hpp"
#include "formats/VmdLoader.hpp"
#include "platform/ImageIO.hpp"
#include "platform/Renderer.hpp"
#include "platform/SokolConfig.hpp"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include "ui/Editor.hpp"
#include "ui/ModelInstance.hpp"
#include "ui/Theme.hpp"
#include "util/sokol_imgui.h"

namespace {

using namespace ms;

// Background colours, matching the Blender dark theme.
constexpr vec3 kViewportBg{0.20f, 0.20f, 0.22f};
constexpr vec3 kWindowBg{0.17f, 0.17f, 0.17f};

Renderer g_renderer;
Editor g_editor;
ModelInstance g_model;
GpuMesh g_cube;
GpuMesh g_grid;
RenderTarget g_viewport;
int g_frame_count = 0;

// A PMX path supplied on the command line (--pmx <path>). sokol_main runs
// before the GL context exists, so the load itself is deferred to init_cb.
std::string g_pmx_path;

// A VMD path supplied on the command line (--vmd <path>). Loaded after the
// model, in init_cb, so the motion is applied to a rig that already exists.
std::string g_vmd_path;

// A .msentara project path supplied on the command line (--project <path>).
// A project supersedes --pmx/--vmd: it names both files itself, so it is
// loaded last and its paths win.
std::string g_project_path;

// Headless-ish export driven from the command line (--export <dir>): render the
// frame range once the window and viewport exist, then quit. Used for
// scripted/CI rendering and for the export smoke test.
std::string g_export_dir;
int g_export_start = -1;
int g_export_end = -1;
bool g_export_use_camera = false;

// Optional frame limit, used by the headless smoke test to exit on its own.
// Unset (or 0) means "run until the user quits".
int g_frame_limit = 0;

// Set once the command-line export has run, so it fires exactly once.
bool g_export_done = false;

// Offscreen target size, recomputed each frame from the viewport panel.
int g_target_w = 0;
int g_target_h = 0;

void apply_camera_navigation();

// Loads a model, replacing any previous one, and frames the camera on it.
void load_model(const std::string& path) {
    if (path.empty()) {
        return;
    }
    std::string error;
    if (!g_model.load(path, g_renderer, error)) {
        g_editor.set_status("Failed to load " + path + ": " + error);
        std::fprintf(stderr, "MSentara: failed to load '%s': %s\n",
                     path.c_str(), error.c_str());
        return;
    }

    // Frame the camera on the model so it is visible immediately regardless
    // of the units the model was authored in.
    const Bounds bounds = compute_bounds(g_model.model().mesh);
    if (bounds.valid()) {
        g_editor.camera().frame(bounds.center, bounds.radius);
    }
    g_editor.set_status("Loaded " + path);
    std::printf("MSentara: loaded '%s' (%zu vertices, %zu bones, %zu materials, "
                "%zu morphs, %zu IK chains, %zu rigid bodies, %zu joints)\n",
                path.c_str(), g_model.model().mesh.vertices.size(),
                g_model.model().skeleton.size(),
                g_model.model().materials.size(),
                g_model.model().morphs.size(),
                g_model.model().skeleton.ik_solvers().size(),
                g_model.model().rigid_bodies.bodies.size(),
                g_model.model().rigid_bodies.joints.size());
}

// Loads a VMD motion onto the currently loaded model, replacing any previous
// clip, and resizes the timeline to the motion's frame range.
void load_motion(const std::string& path) {
    if (path.empty()) {
        return;
    }
    if (!g_model.valid()) {
        g_editor.set_status("Load a PMX model before a VMD motion");
        std::fprintf(stderr, "MSentara: cannot load VMD '%s' without a model\n",
                     path.c_str());
        return;
    }

    VmdResult result = load_vmd(path);
    if (!result.ok) {
        g_editor.set_status("Failed to load VMD " + path + ": " + result.error);
        std::fprintf(stderr, "MSentara: failed to load VMD '%s': %s\n",
                     path.c_str(), result.error.c_str());
        return;
    }

    const int end = result.clip.frame_end();
    result.clip.name = path;
    const std::size_t bone_tracks = result.clip.bone_tracks.size();
    const std::size_t scalar_tracks = result.clip.scalar_tracks.size();
    g_model.set_clip(std::move(result.clip));
    g_model.set_motion_path(path);

    // The timeline drives the motion; start at frame 0 and span the clip.
    Timeline& timeline = g_editor.timeline();
    timeline.frame_start = 0;
    timeline.frame_end = (end > 0) ? end : 1;
    timeline.fps = 30.0f;
    timeline.set_frame(0.0f);
    g_model.apply_animation(timeline.current_frame);

    g_editor.set_status("Loaded VMD " + path);
    std::printf("MSentara: loaded VMD '%s' (%zu bone tracks, %zu scalar tracks, "
                "%d frames)\n",
                path.c_str(), bone_tracks, scalar_tracks, end);
}

// Loads a .msentara project: model, then motion, then the editor state and the
// name-keyed pose overrides. Model and motion paths are stored verbatim in the
// project, so they are used as-is (a project is expected to be opened from the
// directory it was authored in, or to hold absolute paths).
void load_project_file(const std::string& path) {
    if (path.empty()) {
        return;
    }
    Project project;
    std::string error;
    if (!load_project(path, project, error)) {
        g_editor.set_status("Failed to open project " + path + ": " + error);
        std::fprintf(stderr, "MSentara: failed to open project '%s': %s\n",
                     path.c_str(), error.c_str());
        return;
    }

    if (!project.model_path.empty()) {
        load_model(project.model_path);
    }
    if (!project.motion_path.empty() && g_model.valid()) {
        load_motion(project.motion_path);
    }

    // Timeline and camera come from the editor itself.
    g_editor.apply_project_view(project);

    if (g_model.valid()) {
        g_model.set_ik_enabled(project.ik_enabled);
        g_model.set_physics_enabled(project.physics_enabled);
        for (const ProjectMorphWeight& morph : project.morphs) {
            g_model.set_morph_weight(morph.name, morph.weight);
        }
        for (const ProjectBonePose& bone : project.bones) {
            Transform delta;
            delta.position = bone.position;
            delta.rotation = bone.rotation;
            delta.scale = bone.scale;
            g_model.set_bone_delta(bone.name, delta);
        }
        // Re-apply the frame now that the overrides are in, so the reopened
        // pose matches what was saved. Then reset the simulation to a known
        // state rather than whatever the load's evaluate() left behind.
        if (g_model.has_animation()) {
            g_model.apply_animation(g_editor.timeline().current_frame);
        }
        g_model.reset_physics();
    }

    g_editor.set_status("Opened project " + path);
    std::printf("MSentara: opened project '%s' (model='%s', motion='%s')\n",
                path.c_str(), project.model_path.c_str(),
                project.motion_path.c_str());
}

// Saves the current editor state as a .msentara project.
void save_project_file(const std::string& path) {
    if (path.empty()) {
        return;
    }
    const std::string model_path =
        g_model.valid() ? g_model.source_path() : std::string();
    const std::string motion_path =
        g_model.valid() ? g_model.motion_path() : std::string();
    const Project project =
        g_editor.capture_project(model_path, motion_path);

    std::string error;
    if (!save_project(path, project, error)) {
        g_editor.set_status("Failed to save project " + path + ": " + error);
        std::fprintf(stderr, "MSentara: failed to save project '%s': %s\n",
                     path.c_str(), error.c_str());
        return;
    }
    g_editor.set_status("Saved project " + path);
    std::printf("MSentara: saved project '%s'\n", path.c_str());
}

// Draws the scene (grid, scene entities, loaded model) into the currently
// active pass, using the editor camera. Shared by the interactive frame and the
// offline exporter so the two always agree.
void render_scene_into_pass() {
    const float aspect =
        (g_viewport.height > 0)
            ? static_cast<float>(g_viewport.width) /
                  static_cast<float>(g_viewport.height)
            : 1.0f;
    const mat4 view_proj = g_editor.camera().view_projection(aspect);

    g_renderer.draw_lines(g_grid, mat4(1.0f), view_proj);
    for (const Entity& e : g_editor.scene().entities()) {
        if (e.type != EntityType::Mesh || !e.visible) {
            continue;
        }
        const mat4 model = g_editor.scene().world_matrix(e.id);
        g_renderer.draw_triangles(g_cube, model, view_proj);
    }
    if (g_model.valid()) {
        g_model.draw(g_renderer, view_proj);
    }
}

// ---------------------------------------------------------------------------
// Offline PNG-sequence export
//
// One frame is rendered per frame_cb, not in a loop. sokol_gfx forbids writing
// a transient buffer twice in the same frame (that is how the skinned vertices
// reach the GPU), so a multi-frame export has to be spread across frames: each
// frame_cb advances the exporter by exactly one rendered, written PNG. The
// synchronous part of the app never runs while an export is active.
// ---------------------------------------------------------------------------

struct ExportSession {
    ExportRequest request;
    bool active = false;
    bool finished = false;
    int frame = 0;
    int written = 0;
    int width = 0;   // pinned for the whole sequence so every PNG matches
    int height = 0;
    std::string message;
};

ExportSession g_export;
std::vector<std::uint8_t> g_export_pixels;

void start_export(const ExportRequest& request) {
    g_export = ExportSession{};
    g_export.request = request;
    if (request.directory.empty()) {
        g_export.finished = true;
        g_export.message = "Export: no output directory given";
        return;
    }
    if (!g_viewport.valid()) {
        g_export.finished = true;
        g_export.message = "Export: viewport target is not ready";
        return;
    }
    // Create the output directory if it does not exist yet.
    if (::mkdir(request.directory.c_str(), 0755) != 0) {
        struct stat st;
        if (::stat(request.directory.c_str(), &st) != 0 ||
            !S_ISDIR(st.st_mode)) {
            g_export.finished = true;
            g_export.message = "Export: cannot create " + request.directory;
            return;
        }
    }
    g_export.frame = std::min(request.frame_start, request.frame_end);
    // An explicit size overrides the viewport panel; otherwise the export uses
    // whatever size the panel currently has. Either way the size is pinned for
    // the whole sequence so every PNG matches.
    if (request.width > 0 && request.height > 0) {
        g_renderer.resize_render_target(g_viewport, request.width,
                                        request.height);
        g_target_w = request.width;
        g_target_h = request.height;
    }
    g_export.width = g_viewport.width;
    g_export.height = g_viewport.height;
    g_export.active = true;
    g_export.message = "Exporting...";
    g_editor.set_status("Exporting to " + request.directory + "...");
}

// Advances the export by one frame: evaluates the pose and (when enabled) the
// motion's camera track, skins the model, draws into the offscreen target,
// reads the pixels back and writes one PNG. Returns true while more frames
// remain. Must be called outside any pass, before the interactive render.
bool step_export() {
    if (!g_export.active) {
        return false;
    }
    const int last =
        std::max(g_export.request.frame_start, g_export.request.frame_end);
    if (g_export.frame > last) {
        g_export.active = false;
        g_export.finished = true;
        return false;
    }

    const int frame = g_export.frame;
    const float f = static_cast<float>(frame);

    if (g_model.valid()) {
        if (g_model.has_animation()) {
            g_model.apply_animation(f);
        }
        // Re-skin and re-upload without advancing physics: an export must be
        // reproducible, so a dt of zero keeps the simulation pinned to the
        // pose the animation produced.
        g_model.update(g_renderer, 0.0f);
    }

    if (g_editor.use_vmd_camera() && g_model.valid() &&
        g_model.has_animation() && g_model.clip().has_camera()) {
        apply_camera_clip(g_model.clip(), f, g_editor.camera());
    }

    // The model update's transient vertex-buffer write and the draw below must
    // fall in the same sokol frame, so there is deliberately no sg_commit()
    // between them; the single commit at the end of the step flushes both.
    g_renderer.begin_offscreen_pass(g_viewport, kViewportBg);
    render_scene_into_pass();
    // Read while the pass's framebuffer is still current (sokol unbinds it on
    // end_pass), then flip/convert in memory.
    const bool read_ok =
        g_renderer.read_color_pixels(g_viewport, g_export_pixels);
    g_renderer.end_pass();
    sg_commit();

    if (!read_ok) {
        g_export.active = false;
        g_export.finished = true;
        g_export.message = "Export: framebuffer readback failed";
        return false;
    }

    char name[64];
    std::snprintf(name, sizeof(name), "frame_%04d.png", frame);
    const std::string path = g_export.request.directory + "/" + name;
    if (!write_png_rgba(path, g_export.width, g_export.height,
                        g_export_pixels.data())) {
        g_export.active = false;
        g_export.finished = true;
        g_export.message = "Export: failed to write " + path;
        return false;
    }

    ++g_export.written;
    ++g_export.frame;
    if (g_export.frame > last) {
        g_export.active = false;
        g_export.finished = true;
        g_export.message = "Exported " + std::to_string(g_export.written) +
                           " frames to " + g_export.request.directory;
    } else if (g_export.written % 30 == 0) {
        g_editor.set_status("Exporting frame " + std::to_string(g_export.frame) +
                            " / " + std::to_string(last) + "...");
    }
    return g_export.active;
}

// Applies the export result to the editor status and restores the interactive
// pose/camera once the sequence is done.
void finish_export() {
    if (!g_export.finished) {
        return;
    }
    if (g_model.valid() && g_model.has_animation()) {
        g_model.apply_animation(g_editor.timeline().current_frame);
    }
    g_editor.set_status(g_export.message);
    std::printf("MSentara: %s\n", g_export.message.c_str());
    g_export.finished = false;
}

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
    g_editor.set_model(&g_model);

    g_cube = g_renderer.create_mesh(make_unit_cube());
    g_grid = g_renderer.create_mesh(make_grid(10, 1.0f));

    if (const char* limit = std::getenv("MSENTARA_FRAME_LIMIT")) {
        g_frame_limit = std::atoi(limit);
    }

    // Environment variable as well as the flag, so the smoke test can drive a
    // load without touching argv.
    std::string pmx = g_pmx_path;
    if (pmx.empty()) {
        if (const char* env = std::getenv("MSENTARA_PMX")) {
            pmx = env;
        }
    }
    if (!pmx.empty()) {
        load_model(pmx);
    }

    // A motion needs a model, so it is loaded after (and only after) one.
    std::string vmd = g_vmd_path;
    if (vmd.empty()) {
        if (const char* env = std::getenv("MSENTARA_VMD")) {
            vmd = env;
        }
    }
    if (!vmd.empty()) {
        load_motion(vmd);
    }

    // A project supersedes --pmx/--vmd (it names both), so it is applied last.
    std::string project = g_project_path;
    if (project.empty()) {
        if (const char* env = std::getenv("MSENTARA_PROJECT")) {
            project = env;
        }
    }
    if (!project.empty()) {
        load_project_file(project);
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
    // The panel rect is only known after the UI has laid out once; on the very
    // first frame it is empty, so we fall back to the full window.
    const bool panel_known = (vw >= 1.0f && vh >= 1.0f);
    if (!panel_known) {
        vw = static_cast<float>(sapp_width());
        vh = static_cast<float>(sapp_height());
    }

    const int want_w = static_cast<int>(vw);
    const int want_h = static_cast<int>(vh);
    // Do not resize the offscreen target while an export is running: the whole
    // sequence must render at one size.
    if (!g_export.active && (want_w != g_target_w || want_h != g_target_h)) {
        g_target_w = want_w;
        g_target_h = want_h;
        g_renderer.resize_render_target(g_viewport, want_w, want_h);
    }

    apply_camera_navigation();
    g_editor.timeline().advance(static_cast<float>(sapp_frame_duration()));

    // A load requested from the UI is performed here: this runs outside any
    // pass, which is where buffer/image creation and transient writes belong.
    if (g_editor.has_pending_load()) {
        load_model(g_editor.take_pending_load());
    }
    if (g_editor.has_pending_vmd_load()) {
        load_motion(g_editor.take_pending_vmd_load());
    }
    if (g_editor.has_pending_project_open()) {
        load_project_file(g_editor.take_pending_project_open());
    }
    if (g_editor.has_pending_project_save()) {
        save_project_file(g_editor.take_pending_project_save());
    }
    // An export requested from the UI (or the command line) starts here, once
    // the viewport panel has been sized (g_target_w > 0). Waiting for that is
    // what keeps every frame of the sequence at one size.
    const bool viewport_ready =
        g_viewport.valid() && panel_known && g_target_w > 0 && g_target_h > 0;
    if (viewport_ready && !g_export.active && g_editor.has_pending_export()) {
        start_export(g_editor.take_pending_export());
    }
    if (viewport_ready && !g_export_done && !g_export_dir.empty()) {
        g_export_done = true;
        ExportRequest request;
        request.directory = g_export_dir;
        request.frame_start = (g_export_start >= 0) ? g_export_start
                                                    : g_editor.timeline().frame_start;
        request.frame_end = (g_export_end >= 0) ? g_export_end
                                                : g_editor.timeline().frame_end;
        request.fps = g_editor.timeline().fps;
        if (g_export_use_camera) {
            g_editor.set_use_vmd_camera(true);
        }
        start_export(request);
    }

    // While an export is running it owns the frame: render exactly one frame to
    // the offscreen target and write one PNG, then return. Skipping the
    // interactive render keeps the per-frame transient vertex-buffer write
    // unique (sokol allows only one per frame), which is what forces the export
    // to span frames instead of looping inside a single one.
    if (g_export.active) {
        step_export();
        if (g_export.finished) {
            finish_export();
            // A command-line export quits as soon as its sequence is done.
            if (!g_export_dir.empty()) {
                sapp_request_quit();
            }
        }
        // Still composite a UI frame so the window keeps painting while the
        // export runs; the offscreen target was just updated by step_export().
        simgui_frame_desc_t ef = {};
        ef.width = sapp_width();
        ef.height = sapp_height();
        ef.delta_time = sapp_frame_duration();
        ef.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&ef);
        g_editor.draw(g_viewport);
        simgui_flush();
        g_renderer.begin_swapchain_pass(kWindowBg);
        simgui_draw();
        g_renderer.end_pass();
        sg_commit();
        return;
    }

    // Drive the pose from the active clip when the timeline is playing, or
    // when the transport/scrubber changed the frame. The flag is cleared here
    // so a scrub set during the previous frame's UI is consumed exactly once.
    if (g_model.has_animation() &&
        (g_editor.timeline().playing || g_editor.timeline_changed())) {
        g_model.apply_animation(g_editor.timeline().current_frame);
    }
    g_editor.clear_timeline_changed();

    // Re-evaluate the pose and re-upload the skinned vertices for this frame.
    // Physics advances by the real frame time, so a slow frame steps further
    // rather than the simulation running in slow motion. A request to reset
    // the simulation (from the UI) is honoured before the step.
    if (g_editor.consume_physics_reset()) {
        g_model.reset_physics();
    }
    g_model.update(g_renderer, static_cast<float>(sapp_frame_duration()));

    // Let a loaded motion's camera track drive the viewport when the user has
    // opted in. This runs after navigation so a scrub (which changed the frame)
    // is reflected, and is skipped while the user is actively orbiting so the
    // two do not fight.
    if (g_editor.use_vmd_camera() && g_model.valid() &&
        g_model.has_animation() && g_model.clip().has_camera() &&
        !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
        apply_camera_clip(g_model.clip(), g_editor.timeline().current_frame,
                          g_editor.camera());
    }

    // 1) Render the 3D scene into the offscreen target.
    if (g_viewport.valid()) {
        g_renderer.begin_offscreen_pass(g_viewport, kViewportBg);
        render_scene_into_pass();
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
    g_model.clear(g_renderer);
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
    // Parse the CLI here: argv is only available before the window exists, and
    // the actual loads wait for the GL context in init_cb.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--pmx" || arg == "-p") && i + 1 < argc) {
            g_pmx_path = argv[++i];
        } else if (arg.rfind("--pmx=", 0) == 0) {
            g_pmx_path = arg.substr(6);
        } else if ((arg == "--vmd" || arg == "-m") && i + 1 < argc) {
            g_vmd_path = argv[++i];
        } else if (arg.rfind("--vmd=", 0) == 0) {
            g_vmd_path = arg.substr(6);
        } else if (arg == "--project" && i + 1 < argc) {
            g_project_path = argv[++i];
        } else if (arg.rfind("--project=", 0) == 0) {
            g_project_path = arg.substr(10);
        } else if (arg == "--export" && i + 1 < argc) {
            g_export_dir = argv[++i];
        } else if (arg.rfind("--export=", 0) == 0) {
            g_export_dir = arg.substr(9);
        } else if ((arg == "--export-start") && i + 1 < argc) {
            g_export_start = std::atoi(argv[++i]);
        } else if ((arg == "--export-end") && i + 1 < argc) {
            g_export_end = std::atoi(argv[++i]);
        } else if (arg == "--export-camera") {
            g_export_use_camera = true;
        }
    }

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
