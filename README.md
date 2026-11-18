# MSentara

A cross-platform 2D/3D animation studio. MSentara aims to become an
open-source animation tool with a Blender-like editing experience, with a
particular focus on importing and animating **MikuMikuDance (PMX/VMD)**
assets.

This repository currently contains **Milestones 0–5**: the project skeleton, a
working GPU viewport, the rendering-agnostic core data model, PMX/VMD import
with skeletal skinning, morphs, append transforms, CCD-IK and rigid-body
physics (hair/cloth), a native `.msentara` project format, VMD camera tracks,
offline PNG sequence export, and cross-platform CI.

## Features

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
- **PMX rigid-body physics** (`src/core/Physics.hpp`): a small,
  dependency-free solver for hair, skirts and accessories. It reads PMX rigid
  bodies and joints, keeps kinematic bodies on their animated bones, integrates
  dynamic bodies under gravity with damping, links chains with distance
  constraints, and writes the result back onto the rig (MMD's "physics" and
  "physics + bone" modes). Toggle it per model from the Properties panel.
- A native **`.msentara` project format** (versioned JSON) that saves the
  model/motion paths, timeline, camera and manual morph/bone poses. See
  [`docs/PROJECT_FORMAT.md`](docs/PROJECT_FORMAT.md).
- **VMD camera tracks**: a motion's camera keyframes drive the viewport camera
  (with roll, field of view and perspective/orthographic), toggled from the
  Timeline panel's **VMD Camera** checkbox.
- **Offline PNG sequence export**: render a frame range to numbered
  `frame_NNNN.png` files, from **File ▸ Export PNG Sequence…** or the command
  line, optionally from the motion's camera.
- **Cross-platform CI** (GitHub Actions): Linux (GCC + Clang), Windows
  (MSVC), and an Emscripten WebGL2 bundle.

## Building

Requirements:

- CMake 3.20 or newer
- A C++17 compiler (GCC, Clang, or MSVC)
- Ninja or Make (optional, but recommended)

All third-party dependencies (sokol, Dear ImGui, ImGuizmo, glm, stb,
nlohmann/json) are fetched automatically by CMake `FetchContent`, so no manual
setup is required. The first configure clones the pinned revisions; sokol,
Dear ImGui and ImGuizmo are pinned to exact commits, so their clones are
deliberately *not* shallow (a depth-1 clone cannot check out a commit that is
not a branch tip).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Running

```sh
./build/MSentara
```

Load assets from the command line (the model, motion or project is loaded once
the GL context exists):

```sh
./build/MSentara --pmx model.pmx --vmd motion.vmd
./build/MSentara --project scene.msentara
```

`MSENTARA_PMX`, `MSENTARA_VMD` and `MSENTARA_PROJECT` environment variables are
honoured as fallbacks for the corresponding flags.

### Offline PNG sequence export

Render a frame range to numbered PNGs without touching the UI, then quit:

```sh
./build/MSentara --pmx model.pmx --vmd motion.vmd \
    --export out --export-start 0 --export-end 120 --export-camera
```

| Flag | Meaning |
| --- | --- |
| `--export <dir>` | Output directory (created if missing). Frames are `frame_0000.png`, `frame_0001.png`, … |
| `--export-start <n>` | First frame (defaults to the timeline start). |
| `--export-end <n>` | Last frame, inclusive (defaults to the timeline end). |
| `--export-camera` | Drive the camera from the motion's camera track. |

Every frame in a sequence is written at the same size — the 3D viewport panel's
size. The same render is available interactively from **File ▸ Export PNG
Sequence…**. Offline export is desktop-only (WebGL2 has no synchronous
framebuffer readback).

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
  formats/    File-format parsers (PMX/VMD)
tests/        Unit tests for src/core, driven by CTest
cmake/        Dependency fetching, warning flags, and the web shell
docs/         Format notes (e.g. PROJECT_FORMAT.md, REFERENCES.md)
```

The dependency rule is strict and one-directional: `core` knows nothing about
rendering, `platform` and `ui` may use `core`, and `formats` will produce
`core` data structures only.

## License

MSentara is free software, licensed under the **GNU General Public License,
version 3 or later**. See [`LICENSE`](LICENSE) for the full text.

Third-party libraries keep their own licenses: sokol (zlib), Dear ImGui (MIT),
ImGuizmo (MIT), and glm (MIT).
