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

#include "ui/Theme.hpp"

#include "imgui.h"

namespace ms {

namespace {
ImVec4 rgba(float r, float g, float b, float a = 1.0f) {
    return ImVec4(r, g, b, a);
}
}  // namespace

void apply_blender_dark_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    // Geometry: Blender is fairly compact with square-ish corners.
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(8.0f, 6.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 9.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    // Palette sampled from Blender's default dark theme.
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = rgba(0.87f, 0.87f, 0.87f);
    c[ImGuiCol_TextDisabled] = rgba(0.50f, 0.50f, 0.50f);
    c[ImGuiCol_WindowBg] = rgba(0.17f, 0.17f, 0.17f);
    c[ImGuiCol_ChildBg] = rgba(0.15f, 0.15f, 0.15f);
    c[ImGuiCol_PopupBg] = rgba(0.11f, 0.11f, 0.11f, 0.98f);
    c[ImGuiCol_Border] = rgba(0.11f, 0.11f, 0.11f);
    c[ImGuiCol_BorderShadow] = rgba(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = rgba(0.24f, 0.24f, 0.24f);
    c[ImGuiCol_FrameBgHovered] = rgba(0.31f, 0.31f, 0.31f);
    c[ImGuiCol_FrameBgActive] = rgba(0.35f, 0.35f, 0.35f);
    c[ImGuiCol_TitleBg] = rgba(0.10f, 0.10f, 0.10f);
    c[ImGuiCol_TitleBgActive] = rgba(0.13f, 0.13f, 0.13f);
    c[ImGuiCol_TitleBgCollapsed] = rgba(0.10f, 0.10f, 0.10f, 0.75f);
    c[ImGuiCol_MenuBarBg] = rgba(0.13f, 0.13f, 0.13f);
    c[ImGuiCol_ScrollbarBg] = rgba(0.13f, 0.13f, 0.13f);
    c[ImGuiCol_ScrollbarGrab] = rgba(0.35f, 0.35f, 0.35f);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba(0.42f, 0.42f, 0.42f);
    c[ImGuiCol_ScrollbarGrabActive] = rgba(0.50f, 0.50f, 0.50f);
    c[ImGuiCol_CheckMark] = rgba(0.28f, 0.56f, 0.85f);
    c[ImGuiCol_SliderGrab] = rgba(0.28f, 0.56f, 0.85f);
    c[ImGuiCol_SliderGrabActive] = rgba(0.35f, 0.66f, 0.95f);
    c[ImGuiCol_Button] = rgba(0.26f, 0.26f, 0.26f);
    c[ImGuiCol_ButtonHovered] = rgba(0.33f, 0.33f, 0.33f);
    c[ImGuiCol_ButtonActive] = rgba(0.28f, 0.56f, 0.85f);
    c[ImGuiCol_Header] = rgba(0.24f, 0.24f, 0.24f);
    c[ImGuiCol_HeaderHovered] = rgba(0.31f, 0.31f, 0.31f);
    c[ImGuiCol_HeaderActive] = rgba(0.28f, 0.56f, 0.85f);
    c[ImGuiCol_Separator] = rgba(0.24f, 0.24f, 0.24f);
    c[ImGuiCol_SeparatorHovered] = rgba(0.35f, 0.66f, 0.95f);
    c[ImGuiCol_SeparatorActive] = rgba(0.28f, 0.56f, 0.85f);
    c[ImGuiCol_ResizeGrip] = rgba(0.30f, 0.30f, 0.30f, 0.6f);
    c[ImGuiCol_ResizeGripHovered] = rgba(0.35f, 0.66f, 0.95f, 0.7f);
    c[ImGuiCol_ResizeGripActive] = rgba(0.28f, 0.56f, 0.85f, 0.9f);
    c[ImGuiCol_Tab] = rgba(0.14f, 0.14f, 0.14f);
    c[ImGuiCol_TabHovered] = rgba(0.26f, 0.26f, 0.26f);
    c[ImGuiCol_TabSelected] = rgba(0.22f, 0.22f, 0.22f);
    c[ImGuiCol_TabDimmed] = rgba(0.12f, 0.12f, 0.12f);
    c[ImGuiCol_TabDimmedSelected] = rgba(0.18f, 0.18f, 0.18f);
    c[ImGuiCol_DockingPreview] = rgba(0.28f, 0.56f, 0.85f, 0.7f);
    c[ImGuiCol_DockingEmptyBg] = rgba(0.12f, 0.12f, 0.12f);
    c[ImGuiCol_TextSelectedBg] = rgba(0.28f, 0.56f, 0.85f, 0.4f);
    c[ImGuiCol_TableHeaderBg] = rgba(0.20f, 0.20f, 0.20f);
    c[ImGuiCol_TableBorderStrong] = rgba(0.10f, 0.10f, 0.10f);
    c[ImGuiCol_TableBorderLight] = rgba(0.14f, 0.14f, 0.14f);
    c[ImGuiCol_TableRowBgAlt] = rgba(1.0f, 1.0f, 1.0f, 0.03f);
}

}  // namespace ms
