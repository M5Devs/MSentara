# MSentara

A cross-platform 2D/3D animation studio. MSentara aims to become an
open-source animation tool with a Blender-like editing experience, with a
particular focus on importing and animating **MikuMikuDance (PMX/VMD)**
assets.

This repository currently contains **Milestones 0–6B**: the project skeleton, a
working GPU viewport, the rendering-agnostic core data model, PMX/VMD import
with skeletal skinning, morphs, append transforms, CCD-IK and rigid-body
physics (hair/cloth), a native `.msentara` project format, VMD camera tracks,
offline PNG sequence export, a **2D cutout animation mode** with **bone
binding, a keyframe easing editor and onion skinning**, and cross-platform CI.

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
- **2D cutout animation mode** (`src/core/Layer2D.hpp`): a second editor mode
  where the stage is a stack of image layers. Each layer has a **pivot**
  (rotation/scale origin in image pixels) and a parent, so limbs can be posed
  as a hierarchy. Layers import from PNG/JPG, sort by `z_order`, and animate
  through keyed `position`/`rotation`/`scale`/`opacity` tracks. The same
  camera-style pan/zoom and the ImGuizmo manipulator work in 2D, and the 2D
  scene serialises into the `.msentara` project.
- **Sprite variants** (2D): a layer can hold several named images
  ("closed"/"open"/"blink") and switch between them, either manually from the
  Properties panel or through a stepped `variant` animation track that jumps at
  keyframes. The drawn quad resizes to whichever image is active, so a swap is
  visible immediately.
- **2D layer picking**: click a layer in the viewport to select it. The hit
  test inverts the shared 2D camera projection and each layer's world matrix,
  so a click lands on the top-most layer under the cursor even when it is
  scaled, rotated, parented or bone-bound.
- **2D character presets** (`.mschar2d`): export the selected layer and its
  subtree — images, variants, pivots, bindings — as a reusable preset, then
  import it into any scene. Imported layers get unique names and their own
  textures.
- **2D skeletal binding** (`Layer2D::bound_bone`): bind a cutout layer to any
  bone on the loaded model's rig, with a per-layer offset. A bound layer is
  driven by that bone's posed (and CCD-IK-solved) global transform instead of
  its parent chain, so a hand-cutout follows a rigged arm. Bindings are chosen
  in the Properties panel and saved with the project.
- **Keyframe easing editor**: shape the cubic-Bezier ease of the active
  keyframe — presets (Linear, Ease In/Out, Ease In-Out) or drag the two control
  points on an interactive curve — for a 2D layer channel or a 3D bone key
  (X/Y/Z translation or rotation), from the Timeline panel.
- **Onion skinning** (2D): ghosted previous (blue) and next (red/orange)
  frames drawn behind the current one, with configurable counts and opacity.
  Ghosts are interactive-only — an offline PNG export always contains the
  current frame alone.
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

### 2D cutout mode

Start in 2D mode and import image layers (each `--image2d` adds one cutout
layer, bottom to top):

```sh
./build/MSentara --2d --image2d head.png --image2d body.png --image2d arm.png
```

| Flag | Meaning |
| --- | --- |
| `--2d` | Open in 2D cutout mode instead of the 3D viewport. |
| `--image2d <file>` | Import an image as a 2D layer (PNG/JPG). Repeatable. |
| `--variant <layer>:<name>:<path>` | Attach a named sprite variant to a layer already in the scene. Repeatable. |
| `--preset <file>` | Import a `.mschar2d` character preset on startup. |

`MSENTARA_IMAGE2D` supplies a single image path when no `--image2d` flag is
given, `MSENTARA_PRESET` a preset path, and `MSENTARA_VARIANT` a single variant
spec. A project's `editor_mode` reopens the right mode, and the 2D scene,
tracks, sprite variants and image paths travel with it.

### Audio and lip sync

Load a soundtrack and drive a mouth from it:

```sh
./build/MSentara --pmx model.pmx --audio voice.wav --lipsync MouthOpen
```

| Flag | Meaning |
| --- | --- |
| `--audio <file>` | Load a soundtrack (WAV/MP3/FLAC/OGG) at startup. |
| `--lipsync <morph>` | Build a 3D morph track from the loaded audio on startup. |
| `--lipsync-2d <i,j,…>` | Build a variant track for the selected 2D layer from the audio. |

`MSENTARA_AUDIO`, `MSENTARA_LIPSYNC` and `MSENTARA_LIPSYNC_2D` are the
environment fallbacks. The waveform is drawn along the timeline; **File ▸ Lip
Sync** (in the import menu) generates a mouth track from the loaded audio for a
3D morph or, in 2D mode, a layer's open-mouth sprite variants.

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

In 2D mode there is no orbit: middle-drag pans and the wheel zooms, both in
stage pixels. The gizmo shortcuts are the same, and the manipulator edits the
selected layer's pivot.

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
