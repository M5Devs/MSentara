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

namespace ms {

// ---------------------------------------------------------------------------
// Animated GIF export parameters.
//
// The GIF frame delay is expressed in *centiseconds* (hundredths of a second),
// which is the resolution the format stores. At the studio's default 30 fps a
// frame lasts 3.33 cs, so the delay is rounded — the sequence then plays very
// slightly fast, which is unavoidable in GIF. A delay of 0 makes most viewers
// substitute their own default (often 10 cs), so it is clamped to at least 1.
//
// The maths is pure so it can be unit tested with no GPU, matching the rest of
// src/core. The actual encoding lives in the platform layer (ImageIO), which
// is where every other image writer already lives.
// ---------------------------------------------------------------------------

// Converts a frame rate to a GIF per-frame delay in centiseconds, clamped to
// [1, 65535] (the field is a 16-bit unsigned value). A non-positive or
// non-finite fps falls back to 30 fps.
int gif_delay_centiseconds(float fps);

// True when `path` ends in ".gif" (case-insensitive), so the exporter can tell
// a GIF target from a PNG-sequence directory.
bool has_gif_extension(const std::string& path);

}  // namespace ms
