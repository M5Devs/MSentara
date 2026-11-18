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
#include "core/Scene.hpp"
#include "core/Timeline.hpp"
#include "platform/RenderTypes.hpp"
#include "ui/ModelInstance.hpp"

namespace ms {

// Which manipulator the gizmo is currently showing.
enum class GizmoMode { Translate, Rotate, Scale };

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

    // True while a playback transport control (play/pause/reset/scrub) changed
    // the timeline this frame, so the app can re-apply the animation.
    bool timeline_changed() const { return timeline_changed_; }
    void clear_timeline_changed() { timeline_changed_ = false; }

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
    void draw_timeline_panel();
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
    bool timeline_changed_ = false;
    std::string status_;
    char path_buffer_[512] = {};
    char vmd_path_buffer_[512] = {};

    bool show_demo_ = false;
    bool layout_initialised_ = false;
};

}  // namespace ms
