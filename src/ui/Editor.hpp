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

#include <string>

#include "core/Camera.hpp"
#include "core/ProjectIO.hpp"
#include "core/Scene.hpp"
#include "core/Timeline.hpp"
#include "platform/RenderTypes.hpp"
#include "ui/ModelInstance.hpp"

namespace ms {

// Which manipulator the gizmo is currently showing.
enum class GizmoMode { Translate, Rotate, Scale };

// An offline frame-sequence export request: render frames [frame_start,
// frame_end] (inclusive) into `directory` as PNG files. The app performs the
// render because it owns the renderer; the editor only records the request.
struct ExportRequest {
    std::string directory;
    int frame_start = 1;
    int frame_end = 1;
    int width = 0;   // 0 means "use the current viewport size"
    int height = 0;
    float fps = 30.0f;
};

// Owns the editor's non-graphics state (selection, gizmo mode, panel sizes)
// and draws the whole docking UI for one frame.
class Editor {
public:
    void setup();
    void draw(const RenderTarget& viewport);

    Camera& camera() { return camera_; }
    Scene& scene() { return scene_; }
    Timeline& timeline() { return timeline_; }
    GizmoMode gizmo_mode() const { return gizmo_mode_; }
    void set_gizmo_mode(GizmoMode mode) { gizmo_mode_ = mode; }

    // When true, the loaded motion's camera track drives the viewport camera
    // during playback and export. Off by default so a motion does not silently
    // take over the viewpoint; the timeline panel exposes it as a checkbox.
    bool use_vmd_camera() const { return use_vmd_camera_; }
    void set_use_vmd_camera(bool use) { use_vmd_camera_ = use; }
    bool has_vmd_camera() const;

    // The editor does not own the model; it only reads and edits its pose.
    void set_model(ModelInstance* model) { model_ = model; }
    ModelInstance* model() { return model_; }

    // A load requested from the menu or the path field. The app owns the
    // renderer, so it performs the actual load and reports back via
    // set_status().
    void request_load(const std::string& path) {
        pending_load_ = path;
        has_pending_load_ = true;
    }
    bool has_pending_load() const { return has_pending_load_; }
    std::string take_pending_load() {
        has_pending_load_ = false;
        return pending_load_;
    }

    // A VMD load requested from the menu or the path field. Kept separate from
    // the model load because a motion can be loaded onto an already-loaded
    // model.
    void request_load_vmd(const std::string& path) {
        pending_vmd_ = path;
        has_pending_vmd_ = true;
    }
    bool has_pending_vmd_load() const { return has_pending_vmd_; }
    std::string take_pending_vmd_load() {
        has_pending_vmd_ = false;
        return pending_vmd_;
    }

    // A .msentara project open/save requested from the File menu. As with a
    // model load, the app performs the actual file I/O because it owns the
    // renderer and the model.
    void request_open_project(const std::string& path) {
        pending_project_open_ = path;
        has_pending_project_open_ = true;
    }
    bool has_pending_project_open() const { return has_pending_project_open_; }
    std::string take_pending_project_open() {
        has_pending_project_open_ = false;
        return pending_project_open_;
    }

    void request_save_project(const std::string& path) {
        pending_project_save_ = path;
        has_pending_project_save_ = true;
    }
    bool has_pending_project_save() const { return has_pending_project_save_; }
    std::string take_pending_project_save() {
        has_pending_project_save_ = false;
        return pending_project_save_;
    }

    // An offline frame-sequence export requested from the File menu. The app
    // owns the renderer, so it runs the export loop and reports the result via
    // set_status().
    void request_export(const ExportRequest& request) {
        pending_export_ = request;
        has_pending_export_ = true;
    }
    bool has_pending_export() const { return has_pending_export_; }
    ExportRequest take_pending_export() {
        has_pending_export_ = false;
        return pending_export_;
    }

    // Gathers the current editor state into a serialisable project, recording
    // the given model/motion paths (the editor does not know them). Only
    // non-default morph weights and bone poses are written, so the file stays
    // small and a diff shows exactly what the user changed.
    Project capture_project(const std::string& model_path,
                            const std::string& motion_path) const;

    // Applies a loaded project to the editor's own state (timeline and
    // camera). The model, motion and pose overrides are the app's job, since
    // they need the renderer.
    void apply_project_view(const Project& project);

    // True while a playback transport control (play/pause/reset/scrub) changed
    // the timeline this frame, so the app can re-apply the animation.
    bool timeline_changed() const { return timeline_changed_; }
    void clear_timeline_changed() { timeline_changed_ = false; }

    // Set when the user asks to reset the rigid-body simulation from the UI.
    // The editor does not own the solver (the model does), so it only records
    // the request; the app consumes it once per frame.
    bool consume_physics_reset() {
        const bool requested = physics_reset_requested_;
        physics_reset_requested_ = false;
        return requested;
    }

    void set_status(const std::string& status) { status_ = status; }
    const std::string& status() const { return status_; }

    // Set by the 3D viewport panel so the app can skip camera navigation while
    // the cursor is over the UI.
    bool viewport_hovered() const { return viewport_hovered_; }
    bool viewport_focused() const { return viewport_focused_; }

    // Screen-space rectangle of the viewport panel, in ImGui coordinates.
    void viewport_rect(float& x, float& y, float& w, float& h) const;

private:
    void draw_main_menu_bar();
    void draw_viewport_panel(const RenderTarget& viewport);
    void draw_outliner_panel();
    void draw_properties_panel();
    void draw_morphs_panel();
    void draw_timeline_panel();
    void draw_export_modal();
    void draw_model_tree();
    void draw_bone_properties();
    void draw_model_gizmo(float origin_x, float origin_y, float width,
                          float height);

    Scene scene_;
    Camera camera_;
    Timeline timeline_;
    GizmoMode gizmo_mode_ = GizmoMode::Translate;

    ModelInstance* model_ = nullptr;

    bool viewport_hovered_ = false;
    bool viewport_focused_ = false;
    float viewport_x_ = 0.0f;
    float viewport_y_ = 0.0f;
    float viewport_w_ = 0.0f;
    float viewport_h_ = 0.0f;

    std::string pending_load_;
    bool has_pending_load_ = false;
    std::string pending_vmd_;
    bool has_pending_vmd_ = false;
    std::string pending_project_open_;
    bool has_pending_project_open_ = false;
    std::string pending_project_save_;
    bool has_pending_project_save_ = false;
    ExportRequest pending_export_;
    bool has_pending_export_ = false;
    bool timeline_changed_ = false;
    bool physics_reset_requested_ = false;
    bool use_vmd_camera_ = false;
    std::string status_;
    char path_buffer_[512] = {};
    char vmd_path_buffer_[512] = {};
    char project_path_buffer_[512] = {};
    char export_dir_buffer_[512] = {};
    int export_start_ = 0;
    int export_end_ = 0;
    bool export_modal_open_ = false;

    bool show_demo_ = false;
    bool layout_initialised_ = false;
};

}  // namespace ms
