# MSentara

A cross-platform 2D/3D animation studio. MSentara aims to become an
open-source animation tool with a Blender-like editing experience, with a
particular focus on importing and animating **MikuMikuDance (PMX/VMD)**
assets.

This repository currently contains **Milestone 0**: the project skeleton, a
working GPU viewport, and the core data model.

## Features (Milestone 0)

- Dark, Blender-themed **docking UI** (Dear ImGui, `docking` branch):
  3D Viewport, Outliner, Properties, and a Timeline / dope-sheet stub.
- **3D viewport** rendered off-screen into a texture and displayed inside a
  dockable panel, so the layout behaves like a real DCC application.
- A **floor grid** and a **test cube**, with back-face culling and depth
  testing.
- **Blender-style orbit / pan / zoom** camera (`src/core/Camera.hpp`):
  middle-drag orbits, `Shift`+middle-drag pans, `Ctrl`+middle-drag or the
  wheel zooms, plus numpad view presets.
- **ImGuizmo** manipulator attached to the selected object, with both
  Blender (`G`/`R`/`S`) and DCC (`W`/`E`/`R`) shortcut sets.
- A **rendering-agnostic core** (`src/core`): scene graph, transform,
  camera, timeline, and mesh generation, with unit tests.

## Building

Requirements:

- CMake 3.20 or newer
- A C++17 compiler (GCC, Clang, or MSVC)
- Ninja or Make (optional, but recommended)

All third-party dependencies (sokol, Dear ImGui, ImGuizmo, glm) are fetched
automatically by CMake `FetchContent`, so no manual setup is required.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Running

```sh
./build/MSentara
```

### Tests

The core layer is covered by standalone unit tests registered with CTest.
They need no GPU or window:

```sh
ctest --test-dir build --output-on-failure
```

## Web (Emscripten)

The same sources build for WebGL2/WASM. With the Emscripten SDK on `PATH`:

```sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```

Serve `build-web/MSentara.html` over HTTP (WebGL2 requires a secure or
`localhost` context).

## Controls

| Action | Input |
| --- | --- |
| Orbit | Middle-drag in the viewport |
| Pan | `Shift` + middle-drag |
| Zoom | `Ctrl` + middle-drag, or mouse wheel |
| Front / Right / Top view | `1` / `3` / `7` (numpad) |
| Toggle orthographic | `5` (numpad) |
| Frame all | `.` |
| Translate / Rotate / Scale gizmo | `G`/`W`, `R`/`E`, `S` |
| Play / pause timeline | `Space` |

## Project layout

```
src/
  core/       Scene, camera, timeline, transform, mesh — no rendering code
  platform/   sokol (window, input, gfx) wrapper and the GPU renderer
  ui/         Dear ImGui docking layout, theme, and ImGuizmo wiring
  formats/    File-format parsers (PMX/VMD) — placeholder for now
tests/        Unit tests for src/core, driven by CTest
cmake/        Dependency fetching, warning flags, and the web shell
```

The dependency rule is strict and one-directional: `core` knows nothing about
rendering, `platform` and `ui` may use `core`, and `formats` will produce
`core` data structures only.

## License

MSentara is free software, licensed under the **GNU General Public License,
version 3 or later**. See [`LICENSE`](LICENSE) for the full text.

Third-party libraries keep their own licenses: sokol (zlib), Dear ImGui (MIT),
ImGuizmo (MIT), and glm (MIT).
