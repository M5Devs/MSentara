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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

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
constexpr const char* kMorphsWindow = "Morphs";
constexpr const char* kTimelineWindow = "Timeline";
constexpr const char* kExportModal = "Export PNG Sequence";

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

// The four canonical easing shapes, as CSS-style cubic Beziers (control points
// x1, y1, x2, y2). They are what the preset buttons write.
struct BezierPreset {
    const char* name;
    float x1, y1, x2, y2;
};
constexpr BezierPreset kEasingPresets[] = {
    {"Linear", 0.333333f, 0.333333f, 0.666667f, 0.666667f},
    {"Ease In", 0.42f, 0.0f, 1.0f, 1.0f},
    {"Ease Out", 0.0f, 0.0f, 0.58f, 1.0f},
    {"Ease In-Out", 0.42f, 0.0f, 0.58f, 1.0f},
};

// Draws an interactive cubic-Bezier easing editor for `b` (the curve that eases
// the span starting at a keyframe). `drag` holds which control point is being
// dragged between frames. Presets are one click; the two interior control
// points can also be dragged on the canvas or typed. Works for both 2D and 3D
// keys, because both store the same Bezier.
void draw_bezier_editor(const char* id, Bezier& b, int& drag) {
    ImGui::PushID(id);

    // Preset buttons.
    for (const BezierPreset& preset : kEasingPresets) {
        if (ImGui::SmallButton(preset.name)) {
            b.cp1 = vec2(preset.x1, preset.y1);
            b.cp2 = vec2(preset.x2, preset.y2);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    // The curve canvas. A square so the 0..1 domain and range are 1:1, drawn
    // with y up (so an ease-in-out reads as the usual S shape).
    const float canvas = 180.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##canvas", ImVec2(canvas, canvas));
    const bool canvas_hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const auto to_screen = [&](const vec2& p) {
        return ImVec2(origin.x + p.x * canvas, origin.y + (1.0f - p.y) * canvas);
    };
    const auto to_curve = [&](const ImVec2& p) {
        return vec2((p.x - origin.x) / canvas, 1.0f - (p.y - origin.y) / canvas);
    };

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + canvas, origin.y + canvas),
                      IM_COL32(24, 24, 26, 255));
    dl->AddRect(origin, ImVec2(origin.x + canvas, origin.y + canvas),
                IM_COL32(70, 70, 74, 255));
    // Unit box guide (the linear reference is the diagonal).
    dl->AddLine(to_screen(vec2(0.0f, 0.0f)), to_screen(vec2(1.0f, 1.0f)),
                IM_COL32(70, 70, 74, 120));

    // The curve itself, sampled because the cubic has no closed form in y(x).
    constexpr int kSteps = 48;
    ImVec2 prev = to_screen(vec2(0.0f, 0.0f));
    for (int i = 1; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSteps);
        const ImVec2 cur = to_screen(b.eval(t));
        dl->AddLine(prev, cur, IM_COL32(240, 200, 90, 255), 2.0f);
        prev = cur;
    }

    // Control-point handles. The origin and end are fixed at (0,0)/(1,1), so
    // only the two interior points are interactive.
    const vec2 handles[2] = {b.cp1, b.cp2};
    for (int i = 0; i < 2; ++i) {
        const ImVec2 hp = to_screen(handles[i]);
        const ImVec2 anchor = to_screen(i == 0 ? vec2(0.0f, 0.0f) : vec2(1.0f, 1.0f));
        dl->AddLine(anchor, hp, IM_COL32(120, 120, 130, 200));
        dl->AddCircleFilled(hp, 6.0f, IM_COL32(120, 200, 255, 255));
    }

    // Drag a handle: pick the nearer one on mouse-down, then follow the cursor
    // (clamped to the unit box) until the button is released.
    if (canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float d0 = (mouse.x - to_screen(b.cp1).x) * (mouse.x - to_screen(b.cp1).x) +
                         (mouse.y - to_screen(b.cp1).y) * (mouse.y - to_screen(b.cp1).y);
        const float d1 = (mouse.x - to_screen(b.cp2).x) * (mouse.x - to_screen(b.cp2).x) +
                         (mouse.y - to_screen(b.cp2).y) * (mouse.y - to_screen(b.cp2).y);
        drag = (d0 <= d1) ? 0 : 1;
    }
    if (drag >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const vec2 c = to_curve(ImGui::GetIO().MousePos);
            const vec2 clamped(glm::clamp(c.x, 0.0f, 1.0f),
                               glm::clamp(c.y, 0.0f, 1.0f));
            if (drag == 0) {
                b.cp1 = clamped;
            } else {
                b.cp2 = clamped;
            }
        } else {
            drag = -1;
        }
    }

    // Numeric fields, so a curve can be typed exactly (and read off).
    ImGui::SetNextItemWidth(canvas * 0.5f - 4.0f);
    ImGui::DragFloat2("##cp1", &b.cp1.x, 0.005f, 0.0f, 1.0f, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(canvas * 0.5f - 4.0f);
    ImGui::DragFloat2("##cp2", &b.cp2.x, 0.005f, 0.0f, 1.0f, "%.3f");
    ImGui::TextDisabled("Bezier(%.3f, %.3f, %.3f, %.3f)",
                        static_cast<double>(b.cp1.x),
                        static_cast<double>(b.cp1.y),
                        static_cast<double>(b.cp2.x),
                        static_cast<double>(b.cp2.y));

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

void Editor::set_mode(EditorMode mode) {
    if (mode_ == mode) {
        return;  // already in this mode; do not stomp the current view
    }
    mode_ = mode;
    // The 2D view uses the same Camera but a different meaning for its orbit
    // fields (target.xy is the pan, distance is the zoom in pixels). Reset to
    // a sane 2D framing so switching to 2D does not inherit a 3D orbit that
    // puts everything off-screen.
    if (mode_ == EditorMode::TwoD) {
        camera_.yaw = 0.0f;
        camera_.pitch = 0.0f;
        camera_.roll = 0.0f;
        camera_.orthographic = true;
        // A half-height of ~450px frames a 1920x1080-ish stage at 1:1.
        camera_.distance = 450.0f;
        camera_.target = vec3(0.0f, 0.0f, 0.0f);
    } else {
        camera_.set_view_camera_persp();
        camera_.frame(vec3(0.0f), 1.0f);
    }
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
        // The right column holds the Outliner (top), Morphs (middle) and
        // Properties (bottom).
        ImGuiID right_lower = 0;
        ImGuiID top_right = 0;
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.5f, &right_lower,
                                    &top_right);
        ImGuiID morphs_slot = 0;
        ImGuiID properties_slot = 0;
        ImGui::DockBuilderSplitNode(right_lower, ImGuiDir_Down, 0.5f,
                                    &properties_slot, &morphs_slot);

        ImGuiID centre = 0;
        ImGuiID timeline_slot = 0;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.22f, &timeline_slot,
                                    &centre);

        ImGui::DockBuilderDockWindow(kViewportWindow, centre);
        ImGui::DockBuilderDockWindow(kOutlinerWindow, top_right);
        ImGui::DockBuilderDockWindow(kMorphsWindow, morphs_slot);
        ImGui::DockBuilderDockWindow(kPropertiesWindow, properties_slot);
        ImGui::DockBuilderDockWindow(kTimelineWindow, timeline_slot);
        ImGui::DockBuilderFinish(dockspace_id);
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_NoUndocking);
    ImGui::End();

    draw_viewport_panel(viewport);
    if (mode_ == EditorMode::TwoD) {
        draw_layer2d_outliner();
    } else {
        draw_outliner_panel();
    }
    draw_morphs_panel();
    if (mode_ == EditorMode::TwoD) {
        draw_layer2d_properties();
    } else {
        draw_properties_panel();
    }
    draw_timeline_panel();
    draw_export_modal();

    if (show_demo_) {
        ImGui::ShowDemoWindow(&show_demo_);
    }
}

bool Editor::has_vmd_camera() const {
    return model_ != nullptr && model_->valid() && model_->has_animation() &&
           model_->clip().has_camera();
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
        // Native project files: a path field plus Save/Open, matching the
        // import affordances below until a native file dialog lands.
        ImGui::TextUnformatted("Project (.msentara)");
        ImGui::SetNextItemWidth(320.0f);
        const bool project_submitted = ImGui::InputText(
            "##project_path", project_path_buffer_, sizeof(project_path_buffer_),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Save Project") || project_submitted) {
            if (project_path_buffer_[0] != '\0') {
                request_save_project(project_path_buffer_);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Project")) {
            if (project_path_buffer_[0] != '\0') {
                request_open_project(project_path_buffer_);
            }
        }
        ImGui::Separator();
        // A minimal import affordance: type or paste a path and press Load.
        // A native file dialog is a later milestone.
        ImGui::TextUnformatted("Import PMX");
        ImGui::SetNextItemWidth(320.0f);
        const bool submitted = ImGui::InputText(
            "##pmx_path", path_buffer_, sizeof(path_buffer_),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Load") || submitted) {
            if (path_buffer_[0] != '\0') {
                request_load(path_buffer_);
            }
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Import VMD");
        ImGui::SetNextItemWidth(320.0f);
        const bool vmd_submitted = ImGui::InputText(
            "##vmd_path", vmd_path_buffer_, sizeof(vmd_path_buffer_),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Load VMD") || vmd_submitted) {
            if (vmd_path_buffer_[0] != '\0') {
                request_load_vmd(vmd_path_buffer_);
            }
        }
        ImGui::Separator();
        // A 2D cutout layer is created from an image; switching to 2D mode is
        // offered right here so the imported layer is immediately visible.
        ImGui::TextUnformatted("Import 2D Image (PNG/JPG)");
        ImGui::SetNextItemWidth(320.0f);
        const bool img2d_submitted = ImGui::InputText(
            "##image2d_path", image2d_path_buffer_,
            sizeof(image2d_path_buffer_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Add 2D Layer") || img2d_submitted) {
            if (image2d_path_buffer_[0] != '\0') {
                request_import_image2d(image2d_path_buffer_);
            }
        }
        ImGui::Separator();
        // Reusable 2D character presets: export the selected layer and its
        // subtree as a .mschar2d, or drop a saved one into the scene. The app
        // performs the file I/O and re-decodes the images.
        ImGui::TextUnformatted("2D Character Preset (.mschar2d)");
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##preset_path", preset_path_buffer_,
                         sizeof(preset_path_buffer_));
        const bool can_export_preset =
            mode_ == EditorMode::TwoD && scene2d_ != nullptr &&
            scene2d_->scene().selected_layer() != nullptr;
        ImGui::BeginDisabled(!can_export_preset);
        if (ImGui::Button("Export Preset")) {
            if (preset_path_buffer_[0] != '\0') {
                PresetRequest request;
                request.root = scene2d_->scene().selected();
                request.path = preset_path_buffer_;
                request.name = scene2d_->scene().selected_layer()->name;
                request_export_preset(request);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Import Preset")) {
            if (preset_path_buffer_[0] != '\0') {
                request_import_preset(preset_path_buffer_);
            }
        }
        ImGui::Separator();
        // Offline rendering is a desktop-only feature: the web build has no
        // synchronous framebuffer readback, so the entry is hidden there.
#if !defined(__EMSCRIPTEN__)
        if (ImGui::MenuItem("Export PNG Sequence…")) {
            export_modal_open_ = true;
        }
#endif
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
        if (ImGui::MenuItem("3D Mode", nullptr, mode_ == EditorMode::ThreeD)) {
            set_mode(EditorMode::ThreeD);
        }
        if (ImGui::MenuItem("2D Mode", nullptr, mode_ == EditorMode::TwoD)) {
            set_mode(EditorMode::TwoD);
        }
        ImGui::Separator();
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

// ImGuizmo driven by a bone's model-space global matrix. The gizmo edits a
// world matrix (the model transform composed with the bone's global), and the
// edit is converted back into the rig's local delta.
void Editor::draw_model_gizmo(float origin_x, float origin_y, float width,
                              float height) {
    if (model_ == nullptr || !model_->valid()) {
        return;
    }
    const int bone = model_->selected_bone();
    if (bone < 0 || bone >= static_cast<int>(model_->model().skeleton.size())) {
        return;
    }

    const float aspect = (height > 0.0f) ? width / height : 1.0f;
    mat4 view = camera_.view_matrix();
    mat4 proj = camera_.projection_matrix(aspect);

    ImGuizmo::SetOrthographic(camera_.orthographic);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(origin_x, origin_y, width, height);

    const mat4 model_matrix = model_->model_matrix();
    mat4 world =
        model_matrix * model_->pose().global[static_cast<std::size_t>(bone)];

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (gizmo_mode_ == GizmoMode::Rotate) {
        op = ImGuizmo::ROTATE;
    } else if (gizmo_mode_ == GizmoMode::Scale) {
        op = ImGuizmo::SCALE;
    }

    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op,
                             ImGuizmo::LOCAL, glm::value_ptr(world))) {
        model_->set_bone_global(bone, glm::inverse(model_matrix) * world);
    }
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

        // In 2D mode the gizmo manipulates the selected layer's pixel-space
        // world matrix; the 3D paths below are skipped entirely. A plain click
        // in the viewport (not on a gizmo handle) hit-tests the layers and
        // selects the top-most one under the cursor.
        if (mode_ == EditorMode::TwoD) {
            if (viewport_hovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
                pick_layer_at(origin.x, origin.y, avail.x, avail.y);
            }
            if (scene2d_ != nullptr && scene2d_->scene().selected_layer() !=
                                          nullptr) {
                draw_layer2d_gizmo(origin.x, origin.y, avail.x, avail.y);
            }
        } else if (model_ != nullptr && model_->valid() &&
                   model_->selected_bone() >= 0) {
            draw_model_gizmo(origin.x, origin.y, avail.x, avail.y);
        } else {
            Entity* selected = scene_.selected_entity();
            if (selected != nullptr && selected->type != EntityType::Camera) {
                const float aspect =
                    (avail.y > 0.0f) ? avail.x / avail.y : 1.0f;
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
    }

    ImGui::End();
}

void Editor::draw_outliner_panel() {
    ImGui::Begin(kOutlinerWindow);

    if (model_ != nullptr && model_->valid()) {
        if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextUnformatted(model_->name().c_str());
            ImGui::Separator();
            draw_model_tree();
        }
        ImGui::Spacing();
    }

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

// Recursively lists the skeleton. Bones are a hierarchy, so the tree is
// rendered depth-first from the roots; the indentation is what makes the
// parent/child relationship legible.
void Editor::draw_model_tree() {
    if (model_ == nullptr || !model_->valid()) {
        return;
    }
    const Skeleton& skeleton = model_->model().skeleton;
    if (skeleton.empty()) {
        ImGui::TextDisabled("(no skeleton)");
        return;
    }

    // Children are gathered once so each bone is visited exactly once, which
    // also tolerates the malformed-rig case of an out-of-range parent.
    const int count = static_cast<int>(skeleton.size());
    std::vector<std::vector<int>> children(static_cast<std::size_t>(count));
    std::vector<int> roots;
    for (int i = 0; i < count; ++i) {
        const int parent = skeleton.bone(i).parent;
        if (parent >= 0 && parent < count) {
            children[static_cast<std::size_t>(parent)].push_back(i);
        } else {
            roots.push_back(i);
        }
    }

    std::function<void(int)> visit = [&](int bone) {
        const Bone& b = skeleton.bone(bone);
        const bool selected = (model_->selected_bone() == bone);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children[static_cast<std::size_t>(bone)].empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (selected) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(bone);
        const bool open = ImGui::TreeNodeEx(
            "bone", flags, "%s", b.name.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            model_->select_bone(bone);
        }
        if (open && !children[static_cast<std::size_t>(bone)].empty()) {
            for (int child : children[static_cast<std::size_t>(bone)]) {
                visit(child);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    };

    for (int root : roots) {
        visit(root);
    }
}

void Editor::draw_bone_properties() {
    ModelInstance* m = model_;
    if (m == nullptr || !m->valid()) {
        return;
    }
    const int bone = m->selected_bone();
    if (bone < 0 || bone >= static_cast<int>(m->model().skeleton.size())) {
        return;
    }

    const Bone& b = m->model().skeleton.bone(bone);
    ImGui::Text("Bone: %s", b.name.c_str());
    ImGui::TextDisabled("parent: %s",
                        b.parent >= 0
                            ? m->model().skeleton.bone(b.parent).name.c_str()
                            : "(root)");

    // IK solving can be toggled so the user can compare the solved pose against
    // the plain forward-kinematics one. Only shown when the rig actually has a
    // solver.
    if (!m->model().skeleton.ik_solvers().empty()) {
        bool ik = m->ik_enabled();
        if (ImGui::Checkbox("Solve IK", &ik)) {
            m->set_ik_enabled(ik);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu chains)",
                            m->model().skeleton.ik_solvers().size());
    }

    // Rigid-body simulation (hair, skirts, accessories). The toggle gates the
    // solver without discarding its state, and Reset snaps every dynamic body
    // back to the rest pose.
    if (m->has_physics()) {
        bool physics = m->physics_enabled();
        if (ImGui::Checkbox("Simulate Physics", &physics)) {
            m->set_physics_enabled(physics);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu bodies, %zu joints)", m->physics().size(),
                            m->physics().joint_count());
        if (ImGui::Button("Reset Physics")) {
            physics_reset_requested_ = true;
        }
    }
    ImGui::Separator();

    // Editing the pose delta is the authored value: a rotation here pivots
    // around the bone's own origin, which is what posing intuitively means.
    Pose& pose = m->pose();
    if (ImGui::CollapsingHeader("Pose", ImGuiTreeNodeFlags_DefaultOpen)) {
        Transform& delta = pose.delta[static_cast<std::size_t>(bone)];
        draw_vec3_row("Location", delta.position);
        draw_vec3_row("Rotation", delta.rotation, 0.5f);
        draw_vec3_row("Scale", delta.scale, 0.01f);
        if (ImGui::Button("Reset Bone Pose")) {
            delta = Transform{};
        }
    }

    // The resulting model-space position, read-only, so the effect of the
    // pose edit is visible numerically as well as in the viewport.
    const mat4& global = pose.global[static_cast<std::size_t>(bone)];
    const vec3 world_position(global[3]);
    ImGui::Spacing();
    ImGui::TextDisabled("World position: %.3f, %.3f, %.3f",
                        static_cast<double>(world_position.x),
                        static_cast<double>(world_position.y),
                        static_cast<double>(world_position.z));
}

// Lists every morph the model defines, grouped by kind, each with a slider.
// A slider edit writes straight into the live morph weights, so the mesh
// updates on the next frame. VMD morph tracks (scalar tracks) write the same
// weights during playback, and a manual edit simply overrides until the next
// keyframe.
void Editor::draw_morphs_panel() {
    ImGui::Begin(kMorphsWindow);

    if (model_ == nullptr || !model_->valid()) {
        ImGui::TextDisabled("No model loaded");
        ImGui::End();
        return;
    }

    MorphSet& morphs = model_->morphs();
    if (morphs.empty()) {
        ImGui::TextDisabled("This model has no morphs");
        ImGui::End();
        return;
    }

    if (ImGui::Button("Reset All")) {
        morphs.reset_weights();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu morphs", morphs.size());
    ImGui::Separator();

    std::vector<float>& weights = morphs.weights();
    for (std::size_t i = 0; i < morphs.size(); ++i) {
        const MorphTarget& target = morphs.target(static_cast<int>(i));
        const char* kind = "Vertex";
        switch (target.kind) {
            case MorphTarget::Kind::Vertex: kind = "Vertex"; break;
            case MorphTarget::Kind::Bone:   kind = "Bone";   break;
            case MorphTarget::Kind::Group:  kind = "Group";  break;
        }
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(target.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", kind);
        ImGui::SetNextItemWidth(-1.0f);
        // Clamped to [0, 1]: that is the range MMD morph weights use, and the
        // range the evaluation expects.
        ImGui::SliderFloat("##weight", &weights[i], 0.0f, 1.0f, "%.3f");
        ImGui::PopID();
    }

    ImGui::End();
}

void Editor::draw_properties_panel() {
    ImGui::Begin(kPropertiesWindow);

    // When a model is loaded, its bone takes priority in this panel: that is
    // the object the gizmo is currently posing.
    if (model_ != nullptr && model_->valid() && model_->selected_bone() >= 0) {
        if (ImGui::CollapsingHeader("Model Transform",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            Transform& t = model_->transform();
            draw_vec3_row("Location", t.position);
            draw_vec3_row("Rotation", t.rotation, 0.5f);
            draw_vec3_row("Scale", t.scale, 0.01f);
        }
        ImGui::Spacing();
        draw_bone_properties();
        ImGui::End();
        return;
    }

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
    // next, jump-to-end. Any change here flags the timeline so the app
    // re-applies the animation clip before the next skinning pass.
    if (ImGui::Button("|<")) {
        timeline_.set_frame(static_cast<float>(timeline_.frame_start));
        timeline_changed_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("<")) {
        timeline_.playing = false;
        timeline_.step(-1);
        timeline_changed_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(timeline_.playing ? "Pause" : "Play")) {
        timeline_.playing = !timeline_.playing;
    }
    ImGui::SameLine();
    if (ImGui::Button(">")) {
        timeline_.playing = false;
        timeline_.step(1);
        timeline_changed_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(">|")) {
        timeline_.set_frame(static_cast<float>(timeline_.frame_end));
        timeline_changed_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        timeline_.playing = false;
        timeline_.set_frame(static_cast<float>(timeline_.frame_start));
        timeline_changed_ = true;
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
        timeline_changed_ = true;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("##fps", &timeline_.fps, 0.5f, 1.0f, 240.0f, "%.0f fps");

    ImGui::SameLine();
    ImGui::Checkbox("Loop", &timeline_.loop);

    // Onion skinning is a 2D-viewport tool, so its controls live here only in
    // 2D mode. The app reads these settings when it draws the 2D viewport.
    if (mode_ == EditorMode::TwoD) {
        draw_onion_skin_controls();
    }

    // When the loaded motion carries a camera track, let the user drive the
    // viewport from it. Disabled (and unchecked) when there is no camera track.
    if (has_vmd_camera()) {
        ImGui::SameLine();
        bool use = use_vmd_camera_;
        if (ImGui::Checkbox("VMD Camera", &use)) {
            use_vmd_camera_ = use;
        }
    } else {
        use_vmd_camera_ = false;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("| %d frames  (%.2fs @ %.0f fps)",
                        timeline_.frame_end - timeline_.frame_start,
                        static_cast<double>(timeline_.duration_seconds()),
                        static_cast<double>(timeline_.fps));

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

        // Keyframe ticks for the loaded motion, drawn on a second row so they
        // read as a dope sheet rather than competing with the ruler.
        if (model_ != nullptr && model_->has_animation()) {
            const AnimationClip& clip = model_->clip();
            for (const BoneTrack& track : clip.bone_tracks) {
                for (const BoneKey& key : track.keys) {
                    const float x =
                        ruler_origin.x +
                        static_cast<float>(key.frame - timeline_.frame_start) *
                            px_per_frame;
                    if (x < ruler_origin.x ||
                        x > ruler_origin.x + ruler_size.x) {
                        continue;
                    }
                    dl->AddLine(
                        ImVec2(x, ruler_origin.y + 4.0f),
                        ImVec2(x, ruler_origin.y + 4.0f + 14.0f),
                        IM_COL32(240, 200, 90, 200));
                }
            }
            // Camera keys get their own row and colour so a camera cut is easy
            // to spot against the bone keys.
            for (const CameraKey& key : clip.camera_track.keys) {
                const float x =
                    ruler_origin.x +
                    static_cast<float>(key.frame - timeline_.frame_start) *
                        px_per_frame;
                if (x < ruler_origin.x || x > ruler_origin.x + ruler_size.x) {
                    continue;
                }
                dl->AddLine(
                    ImVec2(x, ruler_origin.y + 22.0f),
                    ImVec2(x, ruler_origin.y + 22.0f + 14.0f),
                    IM_COL32(120, 200, 255, 220));
            }
        }
    }
    ImGui::Dummy(ruler_size);

    // Keyframe easing editor for the active keyframe. It works in both modes:
    // 2D edits the selected layer's channel, 3D the selected bone's key.
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Keyframe Easing")) {
        draw_easing_editor();
    }

    if (model_ != nullptr && model_->has_animation()) {
        ImGui::TextDisabled("Motion: %zu bone tracks, %zu scalar tracks, "
                            "%zu camera keys",
                            model_->clip().bone_tracks.size(),
                            model_->clip().scalar_tracks.size(),
                            model_->clip().camera_track.keys.size());
    } else {
        ImGui::TextDisabled("No motion loaded (File > Import VMD)");
    }

    if (!status_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", status_.c_str());
    }

    ImGui::End();
}

// Opens a modal that collects the offline export parameters (output directory
// and frame range) and files the request. The actual render loop lives in the
// app, because it owns the renderer.
void Editor::draw_export_modal() {
#if defined(__EMSCRIPTEN__)
    export_modal_open_ = false;
    return;
#else
    // Seed the fields from the timeline the first time the modal is opened.
    if (export_modal_open_ && export_start_ == 0 && export_end_ == 0) {
        export_start_ = timeline_.frame_start;
        export_end_ = timeline_.frame_end;
    }

    if (export_modal_open_) {
        ImGui::OpenPopup(kExportModal);
    }
    if (!ImGui::BeginPopupModal(kExportModal, &export_modal_open_,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Output directory");
    ImGui::SetNextItemWidth(420.0f);
    ImGui::InputText("##export_dir", export_dir_buffer_,
                     sizeof(export_dir_buffer_));
    ImGui::TextDisabled(
        "Files are written as frame_0000.png, frame_0001.png, ...");

    ImGui::Spacing();
    ImGui::TextUnformatted("Frame range");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragInt("##export_start", &export_start_, 1.0f, -100000, 100000,
                   "Start %d");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragInt("##export_end", &export_end_, 1.0f, -100000, 100000,
                   "End %d");
    if (export_end_ < export_start_) {
        export_end_ = export_start_;
    }

    ImGui::Spacing();
    if (!has_vmd_camera()) {
        ImGui::TextDisabled(
            "Tip: enable \"VMD Camera\" to render from the motion's camera.");
    }

    ImGui::Separator();
    const bool dir_ok = export_dir_buffer_[0] != '\0';
    ImGui::BeginDisabled(!dir_ok);
    if (ImGui::Button("Export", ImVec2(120.0f, 0.0f))) {
        ExportRequest request;
        request.directory = export_dir_buffer_;
        request.frame_start = export_start_;
        request.frame_end = export_end_;
        request.fps = timeline_.fps;
        request_export(request);
        export_modal_open_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        export_modal_open_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
#endif
}

Project Editor::capture_project(const std::string& model_path,
                                const std::string& motion_path) const {
    Project project;
    project.model_path = model_path;
    project.motion_path = motion_path;

    project.frame_start = timeline_.frame_start;
    project.frame_end = timeline_.frame_end;
    project.fps = timeline_.fps;
    project.current_frame = timeline_.current_frame;
    project.loop = timeline_.loop;

    project.camera.target = camera_.target;
    project.camera.distance = camera_.distance;
    project.camera.yaw = camera_.yaw;
    project.camera.pitch = camera_.pitch;
    project.camera.roll = camera_.roll;
    project.camera.fov_y = camera_.fov_y;
    project.camera.orthographic = camera_.orthographic;
    project.use_vmd_camera = use_vmd_camera_;

    if (model_ != nullptr && model_->valid()) {
        project.ik_enabled = model_->ik_enabled();
        project.physics_enabled = model_->physics_enabled();

        // Only non-zero morph weights are written: a default pose then yields
        // an empty array, and a diff shows exactly which morphs the user
        // touched.
        const MorphSet& morphs = model_->morphs();
        const std::vector<float>& weights = morphs.weights();
        for (std::size_t i = 0; i < morphs.size() && i < weights.size(); ++i) {
            if (weights[i] != 0.0f) {
                project.morphs.push_back({morphs.target(static_cast<int>(i)).name,
                                          weights[i]});
            }
        }

        // Likewise, only bones whose delta differs from the rest pose are
        // written, so an untouched rig serialises to an empty array.
        const Pose& pose = model_->pose();
        const std::vector<Bone>& bones = model_->model().skeleton.bones();
        for (std::size_t i = 0; i < bones.size() && i < pose.delta.size(); ++i) {
            const Transform& delta = pose.delta[i];
            if (delta.position == vec3(0.0f) && delta.rotation == vec3(0.0f) &&
                delta.scale == vec3(1.0f)) {
                continue;
            }
            project.bones.push_back({bones[i].name, delta.position,
                                     delta.rotation, delta.scale});
        }
    }

    project.editor_mode = (mode_ == EditorMode::TwoD) ? "2d" : "3d";

    // Serialise the 2D scene and its tracks. ProjectIO writes only the layer
    // fields that differ from their defaults and skips empty tracks, so a
    // 3D-only project emits two empty arrays and nothing else.
    if (scene2d_ != nullptr) {
        for (const Layer2D& layer : scene2d_->scene().layers()) {
            ProjectLayer2D out;
            out.name = layer.name;
            out.image_path = layer.image_path;
            out.parent = layer.parent;
            out.pivot = layer.pivot;
            out.position = layer.position;
            out.rotation = layer.rotation;
            out.scale = layer.scale;
            out.z_order = layer.z_order;
            out.opacity = layer.opacity;
            out.tint = layer.tint;
            out.visible = layer.visible;
            out.bound_bone = layer.bound_bone;
            out.bind_offset = layer.bind_offset;
            for (const LayerVariant& variant : layer.variants) {
                out.variants.push_back({variant.name, variant.image_path});
            }
            out.active_variant = layer.active_variant;
            project.layers_2d.push_back(std::move(out));
        }

        for (const Layer2DTrack& track : scene2d_->clip().layer_2d_tracks) {
            if (track.empty()) {
                continue;
            }
            ProjectLayer2DTrack out;
            out.name = track.name;
            const auto copy_vec2 = [](const Vec2Track& src,
                                      std::vector<ProjectKey2D>& dst) {
                for (const Vec2Key& k : src.keys) {
                    ProjectKey2D key;
                    key.frame = k.frame;
                    key.value2 = k.value;
                    key.cp1 = k.bezier.cp1;
                    key.cp2 = k.bezier.cp2;
                    dst.push_back(key);
                }
            };
            const auto copy_float = [](const FloatTrack& src,
                                       std::vector<ProjectKey2D>& dst) {
                for (const FloatKey& k : src.keys) {
                    ProjectKey2D key;
                    key.frame = k.frame;
                    key.value = k.value;
                    key.cp1 = k.bezier.cp1;
                    key.cp2 = k.bezier.cp2;
                    dst.push_back(key);
                }
            };
            copy_vec2(track.position, out.position);
            copy_vec2(track.scale, out.scale);
            copy_float(track.rotation, out.rotation);
            copy_float(track.opacity, out.opacity);
            for (const VariantKey& k : track.variant.keys) {
                ProjectKey2D key;
                key.frame = k.frame;
                key.value_int = k.value;
                out.variant.push_back(key);
            }
            project.layer2d_tracks.push_back(std::move(out));
        }
    }

    return project;
}

void Editor::apply_project_view(const Project& project) {
    timeline_.frame_start = project.frame_start;
    timeline_.frame_end = project.frame_end;
    timeline_.fps = project.fps;
    timeline_.loop = project.loop;
    timeline_.playing = false;
    timeline_.set_frame(project.current_frame);

    // Switch mode first (which resets the camera to that mode's default), then
    // restore the saved camera, so a project always reopens on the exact view
    // it was saved from regardless of the mode the editor was already in.
    set_mode(project.editor_mode == "2d" ? EditorMode::TwoD
                                         : EditorMode::ThreeD);

    camera_.target = project.camera.target;
    camera_.distance = project.camera.distance;
    camera_.yaw = project.camera.yaw;
    camera_.pitch = project.camera.pitch;
    camera_.roll = project.camera.roll;
    camera_.fov_y = project.camera.fov_y;
    camera_.orthographic = project.camera.orthographic;
    if (mode_ == EditorMode::TwoD) {
        // A 2D project's camera is the pixel-space one; it is always
        // orthographic and only the pan (target.xy) and zoom (distance) matter.
        camera_.orthographic = true;
    }
    use_vmd_camera_ = project.use_vmd_camera;
}

// ---------------------------------------------------------------------------
// 2D mode panels
// ---------------------------------------------------------------------------

void Editor::draw_layer2d_outliner() {
    ImGui::Begin(kOutlinerWindow);

    Scene2D* scene2d = (scene2d_ != nullptr) ? &scene2d_->scene() : nullptr;

    if (scene2d == nullptr || scene2d->empty()) {
        ImGui::TextDisabled("No 2D layers");
        ImGui::TextDisabled("File > Import 2D Image to add one");
        ImGui::End();
        return;
    }

    // The hierarchy is drawn depth-first from the roots, so a child's
    // indentation shows its parent. A child list is built once per frame, which
    // also tolerates the malformed-rig case of an out-of-range parent.
    const int count = static_cast<int>(scene2d->size());
    std::vector<std::vector<LayerId>> children(static_cast<std::size_t>(count));
    std::vector<LayerId> roots;
    for (int i = 0; i < count; ++i) {
        const LayerId parent = scene2d->layers()[static_cast<std::size_t>(i)].parent;
        if (parent >= 0 && parent < count) {
            children[static_cast<std::size_t>(parent)].push_back(i);
        } else {
            roots.push_back(i);
        }
    }

    std::function<void(LayerId)> visit = [&](LayerId id) {
        const Layer2D& layer = scene2d->layers()[static_cast<std::size_t>(id)];
        const bool selected = (scene2d->selected() == id);
        const bool has_children = !children[static_cast<std::size_t>(id)].empty();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!has_children) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (selected) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(id);
        const char* icon = layer.image_path.empty() ? "[G]" : "[I]";
        const bool open = ImGui::TreeNodeEx(
            "layer", flags, "%s %s", icon, layer.name.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            scene2d->select(id);
        }
        if (open && has_children) {
            for (LayerId child : children[static_cast<std::size_t>(id)]) {
                visit(child);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    };

    for (LayerId root : roots) {
        visit(root);
    }

    ImGui::Separator();
    if (ImGui::Button("Delete Layer")) {
        // A flat, index-addressed list makes deletion fiddly (every later
        // index shifts), so M6A only exposes it for a leaf; removing the image
        // and clearing the drawable is the common "hide this part" action.
        Layer2D* selected = scene2d->selected_layer();
        if (selected != nullptr) {
            selected->visible = false;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu layers", scene2d->size());

    ImGui::End();
}

void Editor::draw_layer2d_properties() {
    ImGui::Begin(kPropertiesWindow);

    Scene2D* scene2d = (scene2d_ != nullptr) ? &scene2d_->scene() : nullptr;
    Layer2D* layer = (scene2d != nullptr) ? scene2d->selected_layer() : nullptr;
    if (layer == nullptr) {
        ImGui::TextDisabled("No layer selected");
        ImGui::End();
        return;
    }
    const LayerId layer_id = scene2d->selected();

    char name_buf[128];
    std::snprintf(name_buf, sizeof(name_buf), "%s", layer->name.c_str());
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##layer_name", name_buf, sizeof(name_buf))) {
        layer->name = name_buf;
    }

    ImGui::TextDisabled("Image: %s",
                        layer->image_path.empty() ? "(group, no image)"
                                                  : layer->image_path.c_str());
    ImGui::TextDisabled("Size: %.0f x %.0f px",
                        static_cast<double>(layer->active_size().x),
                        static_cast<double>(layer->active_size().y));

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("pivot");
        ImGui::TextUnformatted("Pivot");
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat2("##pivot", &layer->pivot.x, 1.0f, 0.0f, 0.0f, "%.1f");
        ImGui::PopID();

        ImGui::PushID("pos");
        ImGui::TextUnformatted("Position");
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat2("##pos", &layer->position.x, 1.0f, 0.0f, 0.0f, "%.1f");
        ImGui::PopID();

        ImGui::TextUnformatted("Rotation");
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##rot", &layer->rotation, 0.5f, 0.0f, 0.0f, "%.2f°");

        ImGui::TextUnformatted("Scale");
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat2("##scale", &layer->scale.x, 0.01f, 0.0f, 0.0f, "%.3f");

        if (ImGui::Button("Reset Transform")) {
            layer->position = vec2(0.0f);
            layer->rotation = 0.0f;
            layer->scale = vec2(1.0f);
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("Z Order");
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragInt("##z", &layer->z_order, 0.2f, -10000, 10000);

        ImGui::TextUnformatted("Opacity");
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##opacity", &layer->opacity, 0.0f, 1.0f, "%.3f");

        ImGui::TextUnformatted("Tint");
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::ColorEdit4("##tint", &layer->tint.x);

        ImGui::Checkbox("Visible", &layer->visible);
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Sprite Variants", ImGuiTreeNodeFlags_DefaultOpen)) {
        // A layer may hold several named images ("closed"/"open"/"blink") and
        // swap between them at runtime or via a stepped animation track. The
        // combo is the manual switch; the animation section keys it over time.
        ImGui::TextDisabled("Expressions / states for this layer");

        const char* preview = layer->variants.empty()
                                  ? "(single image)"
                                  : layer->variants[static_cast<std::size_t>(
                                        std::max(0, std::min(
                                            layer->active_variant,
                                            static_cast<int>(layer->variants.size()) - 1)))]
                                        .name.c_str();
        ImGui::TextUnformatted("Active");
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##active_variant", preview)) {
            if (layer->variants.empty()) {
                ImGui::TextDisabled("(no variants)");
            }
            for (std::size_t i = 0; i < layer->variants.size(); ++i) {
                const bool selected = (static_cast<int>(i) == layer->active_variant);
                if (ImGui::Selectable(layer->variants[i].name.c_str(), selected)) {
                    set_layer_variant(layer_id, static_cast<int>(i));
                }
            }
            ImGui::EndCombo();
        }

        if (!layer->variants.empty()) {
            ImGui::TextDisabled("%zu variant(s)", layer->variants.size());
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Add variant");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputTextWithHint("##variant_name", "name",
                                 variant_name_buffer_, sizeof(variant_name_buffer_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-90.0f);
        ImGui::InputTextWithHint("##variant_path", "image path (PNG/JPG)",
                                 variant_path_buffer_, sizeof(variant_path_buffer_));
        ImGui::SameLine();
        if (ImGui::Button("Add")) {
            if (variant_path_buffer_[0] != '\0') {
                VariantRequest request;
                request.layer = layer_id;
                request.name = (variant_name_buffer_[0] != '\0')
                                   ? variant_name_buffer_
                                   : std::string("variant");
                request.path = variant_path_buffer_;
                request_add_variant(request);
                variant_name_buffer_[0] = '\0';
                variant_path_buffer_[0] = '\0';
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Skeletal Binding", ImGuiTreeNodeFlags_DefaultOpen)) {
        // A layer can follow a bone on the loaded model's skeleton (a 3D rig or
        // a 2D rig). When bound, its transform is driven by that bone, so a
        // cutout part tracks a posed (and CCD-IK-solved) bone on the Timeline.
        const bool has_skeleton = model_ != nullptr && model_->valid() &&
                                  !model_->model().skeleton.empty();
        if (!has_skeleton) {
            ImGui::TextDisabled("Load a model to bind layers to bones");
        } else {
            const Skeleton& skeleton = model_->model().skeleton;
            // A combo listing every bone, plus an explicit "(unbound)" entry.
            const std::string current = layer->bound_bone;
            const char* preview =
                current.empty() ? "(unbound)" : current.c_str();
            ImGui::TextUnformatted("Bone");
            ImGui::SameLine(90.0f);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##bound_bone", preview)) {
                if (ImGui::Selectable("(unbound)", current.empty())) {
                    layer->bound_bone.clear();
                }
                for (const Bone& bone : skeleton.bones()) {
                    const bool selected = (bone.name == current);
                    if (ImGui::Selectable(bone.name.c_str(), selected)) {
                        layer->bound_bone = bone.name;
                    }
                }
                ImGui::EndCombo();
            }
            if (!layer->bound_bone.empty()) {
                ImGui::TextUnformatted("Offset");
                ImGui::SameLine(90.0f);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat2("##bind_offset", &layer->bind_offset.x, 1.0f,
                                  0.0f, 0.0f, "%.1f");
                if (ImGui::Button("Unbind")) {
                    layer->bound_bone.clear();
                    layer->bind_offset = vec2(0.0f);
                }
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("2D Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Key at frame %d", current_frame_int());
        if (ImGui::Button("Key Layer")) {
            key_selected_layer2d();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Key")) {
            delete_key_selected_layer2d();
        }

        if (scene2d_ != nullptr) {
            const Layer2DTrack* track =
                scene2d_->clip().find_layer_2d(layer->name);
            if (track != nullptr) {
                ImGui::TextDisabled(
                    "%zu pos / %zu scale / %zu rot / %zu opacity keys",
                    track->position.keys.size(), track->scale.keys.size(),
                    track->rotation.keys.size(), track->opacity.keys.size());
                if (!track->variant.empty()) {
                    ImGui::TextDisabled("%zu variant keys",
                                        track->variant.keys.size());
                }
            } else {
                ImGui::TextDisabled("(no keys for this layer)");
            }
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Keyframe Easing")) {
            draw_easing_editor();
        }
    }

    ImGui::End();
}

// Hit-tests a click against the 2D layers. The viewport panel draws the sprite
// pass with the shared 2D camera projection (identity view), so the inverse of
// that projection turns the cursor's normalised panel position into the same
// pixel-space world point the layers are drawn in. The core scene then picks
// the top-most layer whose quad contains it.
bool Editor::pick_layer_at(float viewport_origin_x, float viewport_origin_y,
                           float width, float height) {
    if (scene2d_ == nullptr || width <= 1.0f || height <= 1.0f) {
        return false;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float u = (mouse.x - viewport_origin_x) / width;
    const float v = (mouse.y - viewport_origin_y) / height;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return false;
    }
    const float aspect = width / height;
    const vec2 world = camera_.screen_to_world_2d(vec2(u, v), aspect);

    // A loaded model's skeleton drives bone-bound layers, so pass its pose so a
    // bound cutout is picked where it is drawn.
    const Skeleton* skeleton = nullptr;
    const Pose* pose = nullptr;
    if (model_ != nullptr && model_->valid()) {
        skeleton = &model_->model().skeleton;
        pose = &model_->pose();
    }
    Scene2D& scene2d = scene2d_->scene();
    const LayerId hit = scene2d.pick_layer(world, skeleton, pose);
    if (hit != kInvalidLayer) {
        scene2d.select(hit);
        return true;
    }
    return false;
}

// Switches a layer to a variant and resizes its quad to that variant's image.
// The size was captured when the image was decoded, so the swap needs no
// renderer here; the app re-evaluates the clip and the next draw samples the
// new texture.
void Editor::set_layer_variant(LayerId id, int variant_index) {
    if (scene2d_ == nullptr) {
        return;
    }
    Layer2D* layer = scene2d_->scene().find(id);
    if (layer == nullptr || layer->variants.empty()) {
        return;
    }
    if (variant_index < 0 ||
        static_cast<std::size_t>(variant_index) >= layer->variants.size()) {
        return;
    }
    layer->active_variant = variant_index;
    const LayerVariant& variant =
        layer->variants[static_cast<std::size_t>(variant_index)];
    if (variant.size.x > 0.0f && variant.size.y > 0.0f) {
        layer->size = variant.size;
    }
    layers2d_changed_ = true;
}

void Editor::draw_layer2d_gizmo(float origin_x, float origin_y, float width,
                                float height) {
    if (scene2d_ == nullptr) {
        return;
    }
    Scene2D& scene2d = scene2d_->scene();
    const LayerId id = scene2d.selected();
    const Layer2D* layer = scene2d.find(id);
    if (layer == nullptr) {
        return;
    }

    const float aspect = (height > 0.0f) ? width / height : 1.0f;
    // 2D mode uses an identity view and the pixel-space projection, so the
    // gizmo sits exactly on the drawn layer.
    const mat4 view(1.0f);
    const mat4 proj = camera_.projection_matrix_2d(aspect);

    ImGuizmo::SetOrthographic(true);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(origin_x, origin_y, width, height);

    // A bone-bound layer is posed by its bone, so it is not directly editable;
    // the gizmo is drawn at the layer's pivot for reference but its edits are
    // ignored. An unbound layer uses the pivot-origin world matrix (parent
    // pivot chain * local pivot matrix).
    const bool bound = !layer->bound_bone.empty();
    const mat4 base = scene2d.pivot_base_matrix(id);
    mat4 world = scene2d.pivot_world_matrix(id);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (gizmo_mode_ == GizmoMode::Rotate) {
        op = ImGuizmo::ROTATE;
    } else if (gizmo_mode_ == GizmoMode::Scale) {
        op = ImGuizmo::SCALE;
    }

    if (!bound && ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                       op, ImGuizmo::LOCAL,
                                       glm::value_ptr(world))) {
        const mat4 local = glm::inverse(base) * world;
        Layer2D* editable = scene2d.find(id);
        editable->position = vec2(local[3].x, local[3].y);

        // Recover the Z rotation and the XY scale from the matrix columns.
        const vec3 col0(local[0]);
        const vec3 col1(local[1]);
        const float sx = glm::length(vec2(col0.x, col0.y));
        const float sy = glm::length(vec2(col1.x, col1.y));
        // A negative determinant means the layer was flipped by the manipulator.
        const float det = local[0][0] * local[1][1] - local[0][1] * local[1][0];
        editable->scale = vec2(sx, det < 0.0f ? -sy : sy);
        editable->rotation = glm::degrees(std::atan2(col0.y, col0.x));
    }
}

void Editor::key_selected_layer2d() {
    if (scene2d_ == nullptr) {
        return;
    }
    Layer2D* layer = scene2d_->scene().selected_layer();
    if (layer == nullptr) {
        return;
    }
    const int frame = current_frame_int();
    Layer2DTrack& track = scene2d_->clip().layer_2d(layer->name);
    track.position.set_key(frame, layer->position);
    track.scale.set_key(frame, layer->scale);
    track.rotation.set_key(frame, layer->rotation);
    track.opacity.set_key(frame, layer->opacity);
    // The variant is keyed only for a layer that has variants, so a plain layer
    // does not grow a stepped channel that would pin it to index 0.
    if (!layer->variants.empty()) {
        track.variant.set_key(frame, layer->active_variant);
    }
    layers2d_changed_ = true;
    // The timeline's end frame follows the keys so a keyed frame is reachable.
    timeline_.frame_end = std::max(timeline_.frame_end, frame);
}

void Editor::delete_key_selected_layer2d() {
    if (scene2d_ == nullptr) {
        return;
    }
    Layer2D* layer = scene2d_->scene().selected_layer();
    if (layer == nullptr) {
        return;
    }
    const int frame = current_frame_int();
    for (Layer2DTrack& track : scene2d_->clip().layer_2d_tracks) {
        if (track.name != layer->name) {
            continue;
        }
        track.position.remove_key(frame);
        track.scale.remove_key(frame);
        track.rotation.remove_key(frame);
        track.opacity.remove_key(frame);
        track.variant.remove_key(frame);
        break;
    }
    layers2d_changed_ = true;
}

// The Bezier that eases the span starting at the active keyframe of the
// selected 2D layer's active channel, or nullptr when there is no such key.
// The easing editor writes through the returned pointer, so shaping the curve
// edits the authored key in place.
Bezier* Editor::active_easing_curve() {
    if (scene2d_ == nullptr) {
        return nullptr;
    }
    Layer2D* layer = scene2d_->scene().selected_layer();
    if (layer == nullptr) {
        return nullptr;
    }
    Layer2DTrack* track = nullptr;
    for (Layer2DTrack& t : scene2d_->clip().layer_2d_tracks) {
        if (t.name == layer->name) {
            track = &t;
            break;
        }
    }
    if (track == nullptr) {
        return nullptr;
    }
    const int frame = current_frame_int();
    switch (easing_channel_) {
        case Easing2DChannel::Position:
            for (Vec2Key& k : track->position.keys) {
                if (k.frame == frame) {
                    return &k.bezier;
                }
            }
            break;
        case Easing2DChannel::Scale:
            for (Vec2Key& k : track->scale.keys) {
                if (k.frame == frame) {
                    return &k.bezier;
                }
            }
            break;
        case Easing2DChannel::Rotation:
            for (FloatKey& k : track->rotation.keys) {
                if (k.frame == frame) {
                    return &k.bezier;
                }
            }
            break;
        case Easing2DChannel::Opacity:
            for (FloatKey& k : track->opacity.keys) {
                if (k.frame == frame) {
                    return &k.bezier;
                }
            }
            break;
    }
    return nullptr;
}

const char* Editor::active_easing_channel_name() const {
    switch (easing_channel_) {
        case Easing2DChannel::Position: return "Position";
        case Easing2DChannel::Scale:    return "Scale";
        case Easing2DChannel::Rotation: return "Rotation";
        case Easing2DChannel::Opacity:  return "Opacity";
    }
    return "Position";
}

// The Keyframe Easing widget: preset buttons plus an interactive control-point
// editor for the curve that eases the active keyframe. In 2D it edits the
// selected layer's active channel; in 3D it edits the selected bone's active
// keyframe curve (X/Y/Z translation or rotation).
void Editor::draw_easing_editor() {
    if (mode_ == EditorMode::TwoD) {
        if (scene2d_ == nullptr) {
            ImGui::TextDisabled("No 2D scene");
            return;
        }
        const char* channels[] = {"Position", "Scale", "Rotation", "Opacity"};
        int channel = static_cast<int>(easing_channel_);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("##easing_channel", &channel, channels,
                         IM_ARRAYSIZE(channels))) {
            easing_channel_ = static_cast<Easing2DChannel>(channel);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("at frame %d", current_frame_int());

        Bezier* curve = active_easing_curve();
        if (curve == nullptr) {
            ImGui::TextDisabled(
                "No %s key at this frame (key the layer first)",
                active_easing_channel_name());
            return;
        }
        draw_bezier_editor("easing2d", *curve, easing_drag_handle_);
        return;
    }

    // 3D: the active keyframe is the selected bone's key at the current frame.
    if (model_ == nullptr || !model_->valid() || !model_->has_animation()) {
        ImGui::TextDisabled("No motion loaded (File > Import VMD)");
        return;
    }
    const int bone = model_->selected_bone();
    const Skeleton& skeleton = model_->model().skeleton;
    if (bone < 0 || bone >= static_cast<int>(skeleton.size())) {
        ImGui::TextDisabled("Select a bone to edit its keyframe easing");
        return;
    }
    const std::string& bone_name = skeleton.bone(bone).name;
    const BoneTrack* track = model_->clip().find_bone(bone_name);
    if (track == nullptr) {
        ImGui::TextDisabled("Bone '%s' has no animation track", bone_name.c_str());
        return;
    }
    const int frame = current_frame_int();
    BoneKey* key = nullptr;
    for (BoneKey& k : model_->clip().bone(bone_name).keys) {
        if (k.frame == frame) {
            key = &k;
            break;
        }
    }
    if (key == nullptr) {
        ImGui::TextDisabled("Bone '%s' has no key at frame %d",
                            bone_name.c_str(), frame);
        return;
    }
    const char* channels[] = {"Translate X", "Translate Y", "Translate Z",
                              "Rotation"};
    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo("##easing_bone_channel", &easing_bone_channel_, channels,
                 IM_ARRAYSIZE(channels));
    ImGui::SameLine();
    ImGui::TextDisabled("%s @ frame %d", bone_name.c_str(), frame);

    Bezier* curve = nullptr;
    switch (easing_bone_channel_) {
        case 0: curve = &key->bezier_x; break;
        case 1: curve = &key->bezier_y; break;
        case 2: curve = &key->bezier_z; break;
        default: curve = &key->bezier_rot; break;
    }
    draw_bezier_editor("easing3d", *curve, easing_drag_handle_);
}

// The Onion Skin toolbar controls (2D only). The app reads these settings when
// it draws the 2D viewport; they are never applied to an offline export.
void Editor::draw_onion_skin_controls() {
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::Checkbox("Onion", &onion_skin_.enabled);
    if (onion_skin_.enabled) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragInt("##onion_prev", &onion_skin_.prev_frames, 0.2f, 0, 20,
                       "Prev %d");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragInt("##onion_next", &onion_skin_.next_frames, 0.2f, 0, 20,
                       "Next %d");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat("##onion_opacity", &onion_skin_.opacity, 0.01f, 0.0f,
                         1.0f, "Alpha %.2f");
    }
}

}  // namespace ms
