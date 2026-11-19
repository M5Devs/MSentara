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

namespace ms {

// Registers the UI fonts: ImGui's embedded default plus, when a system CJK font
// is available, that font merged in so Japanese bone/morph names render as
// glyphs rather than '?'. Call once after simgui_setup(), before the first
// frame. A machine without a CJK font is fine; only CJK glyphs are then
// missing.
void load_ui_fonts();

// Applies a dark, Blender-flavoured colour scheme and spacing to the current
// Dear ImGui context. Call once after simgui_setup().
void apply_blender_dark_theme();

}  // namespace ms
