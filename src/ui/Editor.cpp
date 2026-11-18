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

#include "ui/Editor.hpp"

#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "platform/SokolConfig.hpp"
#include "sokol_app.h"

namespace ms {

namespace {

// Dockable window identifiers. Stable strings matter: ImGui persists the
// layout by hashing these names.
constexpr const char* kViewportWindow = "3D Viewport";
constexpr const char* kOutlinerWindow = "Outliner";
constexpr const char* kPropertiesWindow = "Properties";
constexpr const char* kTimelineWindow = "Timeline";

// The default docking layout: viewport on the left, outliner top-right,
// properties bottom-right, timeline across the bottom.
constexpr ImGuiID kDockNodeMain = 0x4d53656e;  // arbitrary stable seed

void draw_vec3_row(const char* label, vec3& v, float speed = 0.05f) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::DragFloat3("##v", &v.x, speed, 0.0f, 0.0f, "%.3f");
    ImGui::PopID();
}

}  // namespace

void Editor::setup() {
    camera_.frame(vec3(0.0f), 1.0f);
    camera_.set_view_camera_persp();

    // A test cube the user can immediately grab with the gizmo.
    const EntityId cube = scene_.create_entity("Cube", EntityType::Mesh);
    scene_.find(cube)->mesh = "unit_cube";
    scene_.select(cube);

    const EntityId light = scene_.create_entity("Light", EntityType::Light);
    scene_.find(light)->transform.position = vec3(4.0f, 6.0f, 4.0f);

    const EntityId cam = scene_.create_entity("Camera", EntityType::Camera);
    scene_.find(cam)->transform.position = vec3(-5.0f, 3.0f, 5.0f);
}

void Editor::viewport_rect(float& x, float& y, float& w, float& h) const {
    x = viewport_x_;
    y = viewport_y_;
    w = viewport_w_;
    h = viewport_h_;
}

void Editor::draw(const RenderTarget& viewport) {
    draw_main_menu_bar();

    // A full-window host dockspace, with the central node left empty so the
    // viewport panel can occupy it.
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(main_viewport->WorkPos);
    ImGui::SetNextWindowSize(main_viewport->WorkSize);
    ImGui::SetNextWindowViewport(main_viewport->ID);

    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("MSentaraDockHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspace_id = ImGui::GetID("MSentaraDockspace");
    if (!layout_initialised_) {
        layout_initialised_ = true;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(
            dockspace_id, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_NoUndocking);
        ImGui::DockBuilderSetNodeSize(dockspace_id, main_viewport->WorkSize);

        ImGuiID left = 0;
        ImGuiID right = 0;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.72f, &left,
                                    &right);
        ImGuiID bottom = 0;
        ImGuiID top_right = 0;
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.5f, &bottom,
                                    &top_right);

        ImGuiID centre = 0;
        ImGuiID timeline_slot = 0;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.22f, &timeline_slot,
                                    &centre);

        ImGui::DockBuilderDockWindow(kViewportWindow, centre);
        ImGui::DockBuilderDockWindow(kOutlinerWindow, top_right);
        ImGui::DockBuilderDockWindow(kPropertiesWindow, bottom);
        ImGui::DockBuilderDockWindow(kTimelineWindow, timeline_slot);
        ImGui::DockBuilderFinish(dockspace_id);
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_NoUndocking);
    ImGui::End();

    draw_viewport_panel(viewport);
    draw_outliner_panel();
    draw_properties_panel();
    draw_timeline_panel();

    if (show_demo_) {
        ImGui::ShowDemoWindow(&show_demo_);
    }
}

void Editor::draw_main_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        ImGui::MenuItem("New", "Ctrl+N");
        ImGui::MenuItem("Open…", "Ctrl+O", false, false);
        ImGui::MenuItem("Save", "Ctrl+S", false, false);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            sapp_request_quit();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
        ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Front", "Numpad 1");
        ImGui::MenuItem("Right", "Numpad 3");
        ImGui::MenuItem("Top", "Numpad 7");
        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo", nullptr, &show_demo_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("About MSentara", nullptr, false, false);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void Editor::draw_viewport_panel(const RenderTarget& viewport) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::Begin(kViewportWindow);
    ImGui::PopStyleVar();

    viewport_focused_ = ImGui::IsWindowFocused();
    viewport_hovered_ = ImGui::IsWindowHovered();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    viewport_x_ = origin.x;
    viewport_y_ = origin.y;
    viewport_w_ = avail.x;
    viewport_h_ = avail.y;

    if (open && avail.x > 1.0f && avail.y > 1.0f) {
        if (viewport.valid()) {
            ImGui::Image(ImTextureRef(ImTextureID(viewport.imgui_texture_id)),
                         avail, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        } else {
            ImGui::Dummy(avail);
        }

        // Overlay the gizmo on the selected entity, using the exact same
        // view/projection matrices the renderer used.
        Entity* selected = scene_.selected_entity();
        if (selected != nullptr && selected->type != EntityType::Camera) {
            const float aspect = (avail.y > 0.0f) ? avail.x / avail.y : 1.0f;
            mat4 view = camera_.view_matrix();
            mat4 proj = camera_.projection_matrix(aspect);

            ImGuizmo::SetOrthographic(camera_.orthographic);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(origin.x, origin.y, avail.x, avail.y);

            mat4 model = selected->transform.matrix();
            ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
            if (gizmo_mode_ == GizmoMode::Rotate) {
                op = ImGuizmo::ROTATE;
            } else if (gizmo_mode_ == GizmoMode::Scale) {
                op = ImGuizmo::SCALE;
            }

            if (ImGuizmo::Manipulate(
                    glm::value_ptr(view), glm::value_ptr(proj), op,
                    ImGuizmo::LOCAL, glm::value_ptr(model))) {
                selected->transform = Transform::from_matrix(model);
            }
        }
    }

    ImGui::End();
}

void Editor::draw_outliner_panel() {
    ImGui::Begin(kOutlinerWindow);
    ImGui::TextUnformatted("Scene Collection");
    ImGui::Separator();

    for (const Entity& e : scene_.entities()) {
        ImGui::PushID(static_cast<int>(e.id));

        const char* icon = "  ";
        switch (e.type) {
            case EntityType::Mesh:   icon = "[M]"; break;
            case EntityType::Light:  icon = "[L]"; break;
            case EntityType::Camera: icon = "[C]"; break;
            case EntityType::Empty:  icon = "[ ]"; break;
        }

        const bool is_selected = (scene_.selected() == e.id);
        const std::string label = std::string(icon) + " " + e.name;
        if (ImGui::Selectable(label.c_str(), is_selected)) {
            scene_.select(e.id);
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void Editor::draw_properties_panel() {
    ImGui::Begin(kPropertiesWindow);

    Entity* e = scene_.selected_entity();
    if (e == nullptr) {
        ImGui::TextDisabled("No object selected");
        ImGui::End();
        return;
    }

    char name_buf[128];
    std::snprintf(name_buf, sizeof(name_buf), "%s", e->name.c_str());
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##name", name_buf, sizeof(name_buf))) {
        e->name = name_buf;
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_vec3_row("Location", e->transform.position);
        draw_vec3_row("Rotation", e->transform.rotation, 0.5f);
        draw_vec3_row("Scale", e->transform.scale, 0.01f);

        if (ImGui::Button("Reset Transform")) {
            e->transform = Transform{};
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Object", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* type_names[] = {"Empty", "Mesh", "Light", "Camera"};
        int type_index = static_cast<int>(e->type);
        ImGui::TextUnformatted("Type");
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##type", &type_index, type_names,
                         IM_ARRAYSIZE(type_names))) {
            e->type = static_cast<EntityType>(type_index);
        }
        if (e->type == EntityType::Mesh) {
            ImGui::TextUnformatted("Mesh");
            ImGui::SameLine(90.0f);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::TextUnformatted(e->mesh.empty() ? "(none)" : e->mesh.c_str());
        }
        ImGui::Checkbox("Visible", &e->visible);
    }

    ImGui::End();
}

void Editor::draw_timeline_panel() {
    ImGui::Begin(kTimelineWindow);

    // Transport controls, in Blender's order: jump-to-start, previous, play,
    // next, jump-to-end.
    if (ImGui::Button("|<")) {
        timeline_.set_frame(static_cast<float>(timeline_.frame_start));
    }
    ImGui::SameLine();
    if (ImGui::Button("<")) {
        timeline_.step(-1);
    }
    ImGui::SameLine();
    if (ImGui::Button(timeline_.playing ? "Pause" : "Play")) {
        timeline_.playing = !timeline_.playing;
    }
    ImGui::SameLine();
    if (ImGui::Button(">")) {
        timeline_.step(1);
    }
    ImGui::SameLine();
    if (ImGui::Button(">|")) {
        timeline_.set_frame(static_cast<float>(timeline_.frame_end));
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragInt("##start", &timeline_.frame_start, 1.0f, -10000, 10000,
                   "Start %d");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragInt("##end", &timeline_.frame_end, 1.0f, -10000, 10000,
                   "End %d");
    if (timeline_.frame_end <= timeline_.frame_start) {
        timeline_.frame_end = timeline_.frame_start + 1;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    float frame = timeline_.current_frame;
    if (ImGui::SliderFloat("##frame", &frame,
                           static_cast<float>(timeline_.frame_start),
                           static_cast<float>(timeline_.frame_end),
                           "Frame %.0f")) {
        timeline_.playing = false;
        timeline_.set_frame(frame);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("##fps", &timeline_.fps, 0.5f, 1.0f, 240.0f, "%.0f fps");

    ImGui::Separator();

    // A simple keyframe track for the selected entity, drawn as tick marks on
    // a horizontal ruler. Real curve editing arrives in a later milestone.
    ImGui::TextUnformatted("Dope Sheet");
    const ImVec2 ruler_size(ImGui::GetContentRegionAvail().x, 40.0f);
    const ImVec2 ruler_origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ruler_origin,
                      ImVec2(ruler_origin.x + ruler_size.x,
                             ruler_origin.y + ruler_size.y),
                      IM_COL32(28, 28, 28, 255));

    const int span = timeline_.frame_end - timeline_.frame_start;
    if (span > 0 && ruler_size.x > 1.0f) {
        const float px_per_frame = ruler_size.x / static_cast<float>(span);
        for (int f = timeline_.frame_start; f <= timeline_.frame_end; ++f) {
            const float x = ruler_origin.x +
                            static_cast<float>(f - timeline_.frame_start) *
                                px_per_frame;
            const bool major = (f % 10) == 0;
            dl->AddLine(ImVec2(x, ruler_origin.y + ruler_size.y - (major ? 14.0f : 7.0f)),
                        ImVec2(x, ruler_origin.y + ruler_size.y),
                        major ? IM_COL32(140, 140, 140, 255)
                              : IM_COL32(80, 80, 80, 255));
        }
        // Playhead.
        const float hx = ruler_origin.x +
                         (timeline_.current_frame -
                          static_cast<float>(timeline_.frame_start)) *
                             px_per_frame;
        dl->AddLine(ImVec2(hx, ruler_origin.y), ImVec2(hx, ruler_origin.y + ruler_size.y),
                    IM_COL32(90, 170, 255, 255), 2.0f);
    }
    ImGui::Dummy(ruler_size);

    ImGui::End();
}

}  // namespace ms
