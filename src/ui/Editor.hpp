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

#include "core/Camera.hpp"
#include "core/Scene.hpp"
#include "core/Timeline.hpp"
#include "platform/RenderTypes.hpp"

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

    Scene scene_;
    Camera camera_;
    Timeline timeline_;
    GizmoMode gizmo_mode_ = GizmoMode::Translate;

    bool viewport_hovered_ = false;
    bool viewport_focused_ = false;
    float viewport_x_ = 0.0f;
    float viewport_y_ = 0.0f;
    float viewport_w_ = 0.0f;
    float viewport_h_ = 0.0f;

    bool show_demo_ = false;
    bool layout_initialised_ = false;
};

}  // namespace ms
