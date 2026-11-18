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
#if defined(_WIN32)
#include <direct.h>
#endif

#include "core/Animation.hpp"
#include "core/Layer2D.hpp"
#include "core/Mesh.hpp"
#include "core/OnionSkin.hpp"
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
#include "ui/Scene2DInstance.hpp"
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
Scene2DInstance g_scene2d;
GpuMesh g_cube;
GpuMesh g_grid;
GpuMesh g_grid_2d;
GpuMesh g_quad;
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

// One or more 2D image paths supplied on the command line (--image2d <path>,
// repeatable). Each becomes a cutout layer and the editor switches to 2D mode,
// so a 2D scene can be driven from a script or the headless smoke test.
std::vector<std::string> g_image2d_paths;

// Start in 2D mode (--2d) even with no image: useful for a scripted 2D export
// or a project that stores its own mode.
bool g_start_mode_2d = false;

// A .mschar2d preset to import on startup (--preset <path>). Imported after
// any --image2d layers, so a preset can be dropped onto a fresh 2D scene.
std::string g_preset_path;

// Sprite variants to add on startup (--variant <layer>:<name>:<path>,
// repeatable). Each attaches one named variant image to an existing layer, so
// the headless smoke test can exercise the swap path.
std::vector<std::string> g_variant_specs;

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
void apply_project_2d(const Project& project);

// The layer name for an imported image: the file's stem (its name without
// directory or extension), so "assets/head.png" becomes "head". Falls back to
// "Layer" for a path with no stem.
std::string image_layer_name(const std::string& path) {
    std::size_t begin = path.find_last_of("/\\");
    begin = (begin == std::string::npos) ? 0 : begin + 1;
    const std::size_t dot = path.find_last_of('.');
    std::size_t end = (dot == std::string::npos || dot < begin) ? path.size() : dot;
    if (end <= begin) {
        return "Layer";
    }
    return path.substr(begin, end - begin);
}

// Adds a 2D cutout layer from an image file and switches the editor to 2D mode
// so the new layer is immediately visible. The app owns the renderer, so it
// performs the decode/upload; the editor only reads the scene.
LayerId add_image_layer2d(const std::string& path) {
    if (path.empty()) {
        return kInvalidLayer;
    }
    const std::string name = image_layer_name(path);
    const LayerId id =
        g_scene2d.add_layer_from_image(name, path, g_renderer);
    const Layer2D* layer = g_scene2d.scene().find(id);
    if (layer != nullptr && layer->size.x <= 0.0f) {
        g_editor.set_status("Added 2D layer '" + name +
                            "' (image could not be decoded; drawn as a blank)");
        std::fprintf(stderr, "MSentara: could not decode 2D image '%s'\n",
                     path.c_str());
    } else {
        g_editor.set_status("Added 2D layer '" + name + "'");
        std::printf("MSentara: added 2D layer '%s' from '%s' (%g x %g px)\n",
                    name.c_str(), path.c_str(),
                    layer != nullptr ? static_cast<double>(layer->size.x) : 0.0,
                    layer != nullptr ? static_cast<double>(layer->size.y) : 0.0);
    }
    g_editor.set_mode(EditorMode::TwoD);
    g_scene2d.scene().select(id);
    return id;
}

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

    // The 2D scene is rebuilt from the project (images are re-decoded). This is
    // a no-op for a 3D-only project.
    apply_project_2d(project);

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

// Applies a project's 2D state: rebuilds the layer list (images decoded and
// uploaded) and reconstructs the 2D animation clip. Layers are created in
// order so a stored parent index still refers to the right layer.
void apply_project_2d(const Project& project) {
    if (project.layers_2d.empty() && project.layer2d_tracks.empty()) {
        return;
    }

    // Rebuild from scratch: a project is a complete description of the scene.
    g_scene2d.clear(g_renderer);

    for (const ProjectLayer2D& src : project.layers_2d) {
        LayerId id = kInvalidLayer;
        if (!src.image_path.empty()) {
            id = g_scene2d.add_layer_from_image(src.name, src.image_path,
                                                g_renderer, src.parent);
        } else {
            id = g_scene2d.add_group(src.name, src.parent);
        }
        Layer2D* layer = g_scene2d.scene().find(id);
        if (layer == nullptr) {
            continue;
        }
        // The image decode set `size`; the stored fields are the authored
        // values, so they overwrite whatever the defaults were.
        layer->pivot = src.pivot;
        layer->position = src.position;
        layer->rotation = src.rotation;
        layer->scale = src.scale;
        layer->z_order = src.z_order;
        layer->opacity = src.opacity;
        layer->tint = src.tint;
        layer->visible = src.visible;
        // The skeletal binding (empty for an M6 file, which loads unbound).
        layer->bound_bone = src.bound_bone;
        layer->bind_offset = src.bind_offset;
        // Sprite variants, then decode/upload every image the layer needs (the
        // base image plus each variant). add_layer_from_image already uploaded
        // the base; reload covers the variants and re-sizes the quad to the
        // active one.
        for (const ProjectVariant2D& variant : src.variants) {
            layer->variants.push_back(LayerVariant{variant.name,
                                                   variant.image_path, vec2(0.0f)});
        }
        layer->active_variant = src.active_variant;
        if (!layer->variants.empty()) {
            g_scene2d.reload_layer_textures(id, g_renderer);
        }
    }

    // Rebuild the clip's layer tracks.
    g_scene2d.clip() = AnimationClip{};
    for (const ProjectLayer2DTrack& src : project.layer2d_tracks) {
        Layer2DTrack& track = g_scene2d.clip().layer_2d(src.name);
        for (const ProjectKey2D& k : src.position) {
            Bezier b;
            b.cp1 = k.cp1;
            b.cp2 = k.cp2;
            track.position.set_key(k.frame, k.value2, b);
        }
        for (const ProjectKey2D& k : src.scale) {
            Bezier b;
            b.cp1 = k.cp1;
            b.cp2 = k.cp2;
            track.scale.set_key(k.frame, k.value2, b);
        }
        for (const ProjectKey2D& k : src.rotation) {
            Bezier b;
            b.cp1 = k.cp1;
            b.cp2 = k.cp2;
            track.rotation.set_key(k.frame, k.value, b);
        }
        for (const ProjectKey2D& k : src.opacity) {
            Bezier b;
            b.cp1 = k.cp1;
            b.cp2 = k.cp2;
            track.opacity.set_key(k.frame, k.value, b);
        }
        for (const ProjectKey2D& k : src.variant) {
            track.variant.set_key(k.frame, k.value_int);
        }
    }

    if (!g_scene2d.scene().empty()) {
        g_scene2d.scene().select(0);
    }
    // Apply the current frame so the reopened pose matches what was saved.
    if (g_scene2d.has_clip()) {
        apply_layer_2d_clip(g_scene2d.clip(), g_editor.timeline().current_frame,
                            g_scene2d.scene());
    }
    g_editor.set_mode(EditorMode::TwoD);
}

// Adds a named sprite variant to a layer: decodes/uploads the image and
// attaches it. Reports the outcome via the editor status.
void add_layer_variant(const VariantRequest& request) {
    if (request.layer == kInvalidLayer || request.path.empty()) {
        return;
    }
    if (!g_scene2d.add_layer_variant(request.layer, request.name, request.path,
                                     g_renderer)) {
        g_editor.set_status("Could not add variant '" + request.name +
                            "' from '" + request.path + "'");
        std::fprintf(stderr, "MSentara: could not decode variant image '%s'\n",
                     request.path.c_str());
        return;
    }
    g_editor.set_status("Added variant '" + request.name + "'");
    std::printf("MSentara: added sprite variant '%s' from '%s'\n",
                request.name.c_str(), request.path.c_str());
}

// Exports the selected layer's subtree as a .mschar2d character preset.
void export_preset(const PresetRequest& request) {
    if (request.path.empty()) {
        return;
    }
    Char2DPreset preset;
    if (!collect_char2d_preset(g_scene2d.scene(), request.root, request.name,
                               preset)) {
        g_editor.set_status("Export preset: no layer selected");
        return;
    }
    std::string error;
    if (!save_char2d(request.path, preset, error)) {
        g_editor.set_status("Failed to export preset " + request.path + ": " +
                            error);
        std::fprintf(stderr, "MSentara: failed to export preset '%s': %s\n",
                     request.path.c_str(), error.c_str());
        return;
    }
    g_editor.set_status("Exported preset " + request.path);
    std::printf("MSentara: exported 2D character preset '%s' (%zu layers)\n",
                request.path.c_str(), preset.layers.size());
}

// Imports a .mschar2d preset into the scene, decoding each layer's images and
// selecting the new root.
void import_preset(const std::string& path) {
    if (path.empty()) {
        return;
    }
    Char2DPreset preset;
    std::string error;
    if (!load_char2d(path, preset, error)) {
        g_editor.set_status("Failed to import preset " + path + ": " + error);
        std::fprintf(stderr, "MSentara: failed to import preset '%s': %s\n",
                     path.c_str(), error.c_str());
        return;
    }
    const LayerId root = instantiate_char2d_preset(g_scene2d.scene(), preset);
    if (root == kInvalidLayer) {
        g_editor.set_status("Preset " + path + " contained no layers");
        return;
    }
    // Decode/upload every image the imported layers reference (base + variants).
    for (std::size_t i = 0; i < g_scene2d.scene().size(); ++i) {
        const LayerId id = static_cast<LayerId>(i);
        const Layer2D* layer = g_scene2d.scene().find(id);
        if (layer != nullptr &&
            (!layer->image_path.empty() || !layer->variants.empty())) {
            g_scene2d.reload_layer_textures(id, g_renderer);
        }
    }
    g_scene2d.scene().select(root);
    g_editor.set_mode(EditorMode::TwoD);
    g_editor.set_status("Imported preset " + path);
    std::printf("MSentara: imported 2D character preset '%s' (%zu layers)\n",
                path.c_str(), preset.layers.size());
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
// offline exporter so the two always agree. `draw_onion` is set only by the
// interactive path: an offline export must contain the current frame alone.
void render_scene_into_pass(bool draw_onion) {
    const float aspect =
        (g_viewport.height > 0)
            ? static_cast<float>(g_viewport.width) /
                  static_cast<float>(g_viewport.height)
            : 1.0f;

    // 2D mode draws the cutout layers (and a 2D grid) with the pixel-space
    // projection; the 3D grid, cube and model are skipped entirely.
    if (g_editor.mode() == EditorMode::TwoD) {
        const mat4 proj2d = g_editor.camera().projection_matrix_2d(aspect);
        // A loaded model's skeleton can drive bone-bound layers, so pass it
        // through when present (its pose already includes any CCD-IK).
        const Skeleton* skeleton = nullptr;
        const Pose* pose = nullptr;
        if (g_model.valid()) {
            skeleton = &g_model.model().skeleton;
            pose = &g_model.pose();
        }

        // Onion skinning: ghosted neighbouring frames, drawn behind the
        // current frame. Each ghost is a copy of the scene with its keyed
        // layer transforms evaluated at that frame (bone-bound layers keep the
        // current bone pose, which is the common case for a keyframed cutout).
        if (draw_onion) {
            const OnionSkinSettings& onion = g_editor.onion_skin();
            const int current = g_editor.current_frame_int();
            const std::vector<OnionFrame> ghosts = onion_skin_window(
                onion, current, g_editor.timeline().frame_start,
                g_editor.timeline().frame_end);
            for (const OnionFrame& ghost : ghosts) {
                Scene2D copy = g_scene2d.scene();
                if (g_scene2d.has_clip()) {
                    apply_layer_2d_clip(g_scene2d.clip(),
                                        static_cast<float>(ghost.frame), copy);
                }
                g_scene2d.draw_ghost_frame(
                    g_renderer, g_quad, proj2d, copy,
                    onion_ghost_tint(ghost.offset, onion.opacity), skeleton,
                    pose);
            }
        }

        // The 2D grid is a line mesh, so it draws with the line pipeline.
        g_renderer.draw_lines(g_grid_2d, mat4(1.0f), proj2d);
        g_scene2d.draw(g_renderer, g_quad, proj2d, skeleton, pose);
        return;
    }

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

// Creates `dir` if it does not already exist. POSIX mkdir takes a mode; the
// MSVC one (a direct.h shim over _mkdir) takes only the path. Returns true when
// the directory exists afterwards.
bool ensure_directory(const std::string& dir) {
#if defined(_WIN32)
    if (::_mkdir(dir.c_str()) != 0) {
        struct _stat st;
        if (::_stat(dir.c_str(), &st) != 0 || (st.st_mode & _S_IFDIR) == 0) {
            return false;
        }
    }
#else
    if (::mkdir(dir.c_str(), 0755) != 0) {
        struct stat st;
        if (::stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            return false;
        }
    }
#endif
    return true;
}

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
    if (!ensure_directory(request.directory)) {
        g_export.finished = true;
        g_export.message = "Export: cannot create " + request.directory;
        return;
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

    if (g_editor.mode() == EditorMode::TwoD) {
        // 2D export: drive the cutout tracks from the clip at this frame. No
        // GPU upload is needed (the sprites use a shared static quad), so this
        // is a pure CPU evaluation.
        if (g_scene2d.has_clip()) {
            apply_layer_2d_clip(g_scene2d.clip(), f, g_scene2d.scene());
        }
        // Bone-bound layers read the model's pose, so keep it in step with the
        // exported frame too (no skinning needed for a 2D render).
        if (g_model.valid() && g_model.has_animation()) {
            g_model.apply_animation(f);
        }
    } else if (g_model.valid()) {
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
    // An offline export never draws onion-skin ghosts: the sequence must be the
    // current frame alone.
    render_scene_into_pass(false);
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
    g_editor.set_scene2d(&g_scene2d);

    // A --2d flag starts in 2D mode; a loaded project or image may override
    // this later (both switch modes explicitly).
    if (g_start_mode_2d) {
        g_editor.set_mode(EditorMode::TwoD);
    }

    g_cube = g_renderer.create_mesh(make_unit_cube());
    g_grid = g_renderer.create_mesh(make_grid(10, 1.0f));
    // 2D geometry: a shared unit quad every layer is drawn with, and a 100px
    // grid that gives the 2D viewport a sense of scale.
    g_quad = g_renderer.create_mesh(make_unit_quad());
    g_grid_2d = g_renderer.create_mesh(make_grid_2d(10, 100.0f));

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

    // 2D images: each --image2d becomes a layer and switches the editor to 2D.
    // MSENTARA_IMAGE2D supplies one path when no flag was given.
    std::vector<std::string> images = g_image2d_paths;
    if (images.empty()) {
        if (const char* env = std::getenv("MSENTARA_IMAGE2D")) {
            images.emplace_back(env);
        }
    }
    for (const std::string& image : images) {
        add_image_layer2d(image);
    }

    // A .mschar2d preset is imported after any --image2d layers, so a preset
    // can be dropped onto a fresh 2D scene.
    std::string preset = g_preset_path;
    if (preset.empty()) {
        if (const char* env = std::getenv("MSENTARA_PRESET")) {
            preset = env;
        }
    }
    if (!preset.empty()) {
        import_preset(preset);
    }

    // Sprite variants: each --variant <layer>:<name>:<path> attaches one named
    // variant image to a layer already in the scene (by name).
    std::vector<std::string> variant_specs = g_variant_specs;
    if (variant_specs.empty()) {
        if (const char* env = std::getenv("MSENTARA_VARIANT")) {
            variant_specs.emplace_back(env);
        }
    }
    for (const std::string& spec : variant_specs) {
        const std::size_t first = spec.find(':');
        const std::size_t second =
            (first == std::string::npos) ? std::string::npos
                                         : spec.find(':', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            std::fprintf(stderr,
                         "MSentara: --variant expects <layer>:<name>:<path>\n");
            continue;
        }
        const std::string layer_name = spec.substr(0, first);
        const std::string variant_name = spec.substr(first + 1, second - first - 1);
        const std::string path = spec.substr(second + 1);
        // Resolve the layer by name; the first match wins (names are unique
        // within a scene as produced by preset instantiation).
        LayerId target = kInvalidLayer;
        for (std::size_t i = 0; i < g_scene2d.scene().size(); ++i) {
            const Layer2D* layer = g_scene2d.scene().find(static_cast<LayerId>(i));
            if (layer != nullptr && layer->name == layer_name) {
                target = static_cast<LayerId>(i);
                break;
            }
        }
        if (target == kInvalidLayer) {
            std::fprintf(stderr,
                         "MSentara: --variant: no layer named '%s'\n",
                         layer_name.c_str());
            continue;
        }
        VariantRequest request;
        request.layer = target;
        request.name = variant_name;
        request.path = path;
        add_layer_variant(request);
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
    if (g_editor.has_pending_image2d()) {
        add_image_layer2d(g_editor.take_pending_image2d());
    }
    if (g_editor.has_pending_project_open()) {
        load_project_file(g_editor.take_pending_project_open());
    }
    if (g_editor.has_pending_project_save()) {
        save_project_file(g_editor.take_pending_project_save());
    }
    if (g_editor.has_pending_variant()) {
        add_layer_variant(g_editor.take_pending_variant());
    }
    if (g_editor.has_pending_preset_export()) {
        export_preset(g_editor.take_pending_preset_export());
    }
    if (g_editor.has_pending_preset_import()) {
        import_preset(g_editor.take_pending_preset_import());
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
    const bool frame_dirty = g_editor.timeline().playing ||
                             g_editor.timeline_changed() ||
                             g_editor.layers2d_changed();
    if (g_editor.mode() == EditorMode::TwoD) {
        // 2D: re-evaluate the layer tracks at the current frame. The keying
        // tools flag layers2d_changed() so a fresh key is applied immediately.
        if (g_scene2d.has_clip() && frame_dirty) {
            apply_layer_2d_clip(g_scene2d.clip(), g_editor.timeline().current_frame,
                                g_scene2d.scene());
        }
        // Bone-bound layers follow the model's skeleton, so keep its pose in
        // step with the timeline too. No skinning is needed here — only the
        // bone globals a bound layer reads.
        if (g_model.valid() && g_model.has_animation() && frame_dirty) {
            g_model.apply_animation(g_editor.timeline().current_frame);
        }
    } else if (g_model.has_animation() && frame_dirty) {
        g_model.apply_animation(g_editor.timeline().current_frame);
    }
    g_editor.clear_timeline_changed();
    g_editor.clear_layers2d_changed();

    // Re-evaluate the pose and re-upload the skinned vertices for this frame.
    // Physics advances by the real frame time, so a slow frame steps further
    // rather than the simulation running in slow motion. A request to reset
    // the simulation (from the UI) is honoured before the step. The 2D path
    // has no GPU upload (sprites use a shared static quad), so it is skipped.
    if (g_editor.mode() != EditorMode::TwoD) {
        if (g_editor.consume_physics_reset()) {
            g_model.reset_physics();
        }
        g_model.update(g_renderer, static_cast<float>(sapp_frame_duration()));
    }

    // Let a loaded motion's camera track drive the viewport when the user has
    // opted in. This runs after navigation so a scrub (which changed the frame)
    // is reflected, and is skipped while the user is actively orbiting so the
    // two do not fight. 2D mode has no camera track, so this is 3D-only.
    if (g_editor.mode() != EditorMode::TwoD && g_editor.use_vmd_camera() &&
        g_model.valid() && g_model.has_animation() &&
        g_model.clip().has_camera() && !ImGuizmo::IsUsing() &&
        !ImGuizmo::IsOver()) {
        apply_camera_clip(g_model.clip(), g_editor.timeline().current_frame,
                          g_editor.camera());
    }

    // 1) Render the scene (3D or 2D, per the editor mode) into the offscreen
    // target. The interactive path is the only one that draws onion-skin
    // ghosts, so an export stays a clean single frame.
    if (g_viewport.valid()) {
        g_renderer.begin_offscreen_pass(g_viewport, kViewportBg);
        render_scene_into_pass(true);
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
    g_scene2d.clear(g_renderer);
    g_renderer.destroy_mesh(g_cube);
    g_renderer.destroy_mesh(g_grid);
    g_renderer.destroy_mesh(g_grid_2d);
    g_renderer.destroy_mesh(g_quad);
    g_renderer.destroy_render_target(g_viewport);
    simgui_shutdown();
    g_renderer.shutdown();
}

// Blender's viewport navigation: middle-drag orbits, shift+middle-drag pans,
// ctrl+middle-drag (or the wheel) zooms. The gizmo takes priority, so
// navigation is suppressed while a manipulator handle is hovered or dragged.
//
// In 2D mode there is no orbit: middle-drag pans and the wheel zooms, both in
// pixel space (the camera's target.xy and distance carry the 2D pan/zoom).
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

    if (g_editor.mode() == EditorMode::TwoD) {
        // Pixel-space pan/zoom. The 2D projection maps one pixel to one world
        // unit, so a drag moves the view by exactly the cursor delta.
        if (middle) {
            const ImVec2 d = io.MouseDelta;
            cam.target.x -= d.x;
            cam.target.y -= d.y;
        }
        if (io.MouseWheel != 0.0f) {
            // A zoom step shrinks/grows the visible half-height (the pixels the
            // viewport spans vertically).
            cam.distance *= std::pow(0.9f, io.MouseWheel);
            cam.distance = std::max(cam.distance, 1.0f);
        }
        return;
    }

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
            // Tab toggles the 3D/2D viewport, as in Blender's editor switching.
            case SAPP_KEYCODE_TAB:
                editor.set_mode(editor.mode() == EditorMode::TwoD
                                    ? EditorMode::ThreeD
                                    : EditorMode::TwoD);
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
        } else if (arg == "--image2d" && i + 1 < argc) {
            // Repeatable: each occurrence adds one cutout layer.
            g_image2d_paths.emplace_back(argv[++i]);
        } else if (arg.rfind("--image2d=", 0) == 0) {
            g_image2d_paths.push_back(arg.substr(10));
        } else if (arg == "--2d") {
            g_start_mode_2d = true;
        } else if (arg == "--preset" && i + 1 < argc) {
            g_preset_path = argv[++i];
        } else if (arg.rfind("--preset=", 0) == 0) {
            g_preset_path = arg.substr(9);
        } else if (arg == "--variant" && i + 1 < argc) {
            // Format: <layer>:<name>:<path>
            g_variant_specs.emplace_back(argv[++i]);
        } else if (arg.rfind("--variant=", 0) == 0) {
            g_variant_specs.push_back(arg.substr(10));
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
