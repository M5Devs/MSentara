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
    draw_outliner_panel();
    draw_morphs_panel();
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

        // The loaded model takes over the gizmo: its selected bone is posed,
        // and the scene entities' gizmo is only used when no model is present.
        if (model_ != nullptr && model_->valid() &&
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
    ImGui::TextDisabled("World position: %.3f, %.3f, %.3f", world_position.x,
                        world_position.y, world_position.z);
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
        const MorphTarget& target = morphs.target(i);
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
    ImGui::TextDisabled("| %d frames  (%.2fs @ %.0f fps)",
                        timeline_.frame_end - timeline_.frame_start,
                        timeline_.duration_seconds(), timeline_.fps);

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
        }
    }
    ImGui::Dummy(ruler_size);

    if (model_ != nullptr && model_->has_animation()) {
        ImGui::TextDisabled("Motion: %zu bone tracks, %zu scalar tracks",
                            model_->clip().bone_tracks.size(),
                            model_->clip().scalar_tracks.size());
    } else {
        ImGui::TextDisabled("No motion loaded (File > Import VMD)");
    }

    if (!status_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", status_.c_str());
    }

    ImGui::End();
}

}  // namespace ms
