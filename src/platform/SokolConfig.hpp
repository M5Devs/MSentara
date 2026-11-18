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

// Selects the sokol backend. Must be included before any sokol header, in
// every translation unit, so the headers agree on struct layouts.
//
// Desktop uses the OpenGL core profile (GLX/EGL on Linux, WGL on Windows,
// NSOpenGL on macOS). The web build uses WebGL2, which sokol exposes through
// its GLES3 backend.

#if defined(__EMSCRIPTEN__)
#define SOKOL_GLES3
#else
#define SOKOL_GLCORE
#endif
