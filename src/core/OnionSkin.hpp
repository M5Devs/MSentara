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

#include <vector>

#include "core/Math.hpp"

namespace ms {

// ---------------------------------------------------------------------------
// Onion skinning: ghosted neighbouring frames drawn behind the current one.
//
// The setting lives in the editor and is only ever applied to the interactive
// viewport — never to an offline export, which must contain the current frame
// alone. This header holds the pure frame-window maths so it can be unit
// tested with no GPU or window, matching the rest of src/core.
// ---------------------------------------------------------------------------

// The Onion Skin controls the 2D toolbar exposes.
struct OnionSkinSettings {
    bool enabled = false;
    int prev_frames = 2;   // ghost frames before the current one
    int next_frames = 2;   // ghost frames after the current one
    float opacity = 0.35f;  // base alpha of a ghost; falls off with distance
};

// One ghost frame to draw. `offset` is signed: negative for a past frame,
// positive for a future one, and its magnitude is the distance from the
// current frame (used to fade the ghost and to pick its tint).
struct OnionFrame {
    int frame = 0;
    int offset = 0;
};

// The ghost frames for `current_frame`, ordered far-to-near within each side so
// a nearer ghost draws last (on top). Past frames come first (they are "behind"
// the motion), future frames after. Frames outside [frame_start, frame_end] are
// dropped, and the current frame itself is never included. Returns an empty
// list when onion skinning is disabled.
std::vector<OnionFrame> onion_skin_window(const OnionSkinSettings& settings,
                                          int current_frame, int frame_start,
                                          int frame_end);

// The tint applied to a ghost frame `offset` frames away from the current one.
// Past frames are tinted a cool blue, future frames a warm red/orange, matching
// the usual animation convention. `opacity` is the base alpha; the returned
// alpha fades with distance so a far ghost is fainter than a near one.
vec4 onion_ghost_tint(int offset, float opacity);

}  // namespace ms
