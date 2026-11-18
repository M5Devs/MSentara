# MSentara — project memory

Cross-platform 2D/3D animation studio (C++17 / CMake / sokol + Dear ImGui
docking + ImGuizmo + glm). Repo root `MSentara/`, GPLv3. Repo remote is
`M5Devs/MSentara`. Milestones 0–6B complete: skeleton+viewport, PMX loading +
skinning, VMD motion, morphs/append/CCD-IK, `.msentara` project format, CI,
PMX rigid-body physics (hair/cloth), VMD camera tracks + offline PNG
sequence export, a 2D cutout animation mode, and **M6B: 2D bone binding + a
keyframe easing editor + onion skinning**. **Per-milestone detail lives
in the daily logs** (e.g. `2026-11-17.md`); this index keeps only durable,
cross-cutting facts.

## Workflow rule — READ THIS FIRST

**NEVER create feature branches or open Pull Requests (PRs) — always commit
and push directly to `main` (`origin/main`) so the repo stays clean and GitHub
Actions CI triggers automatically.** Work on `main`, verify locally, then
`git commit` + `git push origin main`. Do not `git checkout -b`, do not push a
feature branch, and do not call a create-PR tool. (PRs opened with the repo's
own token also fail to trigger CI because of GitHub's recursion guard, which is
another reason to push to `main` directly.)

## Build memory limit — READ THIS FIRST

The container is OOM-killed during parallel C++ compilation. **Always build
with `-j 1`**, never unbounded. Configure Debug with `-DCMAKE_BUILD_TYPE=Debug
-DCMAKE_CXX_FLAGS=-g0 -DCMAKE_C_FLAGS=-g0` to keep compiler RSS down. Run long
builds in the background and poll the log; foreground commands are capped
~1080 s.

The toolchain does **not** persist across container resets: re-run `pip install
--user cmake ninja` and `apt-get install -y libgl1-mesa-dev libglx-dev
libx11-dev libxcursor-dev libxi-dev libxext-dev libxrandr-dev libxinerama-dev
xvfb x11-utils imagemagick`. `/workspace` persists.

**FetchContent cannot shallow-fetch the pinned sokol SHA** (it is not a branch
tip): a fresh configure dies with `fatal: unable to read tree (f5e88334…)`.
Fix: drop `GIT_SHALLOW` from the commit-pinned clones (sokol/imgui/imguizmo),
pin stb to a SHA, keep shallow/URL fetch for glm (tag) and nlohmann/json
(release zip). See `cmake/MSentaraDependencies.cmake`; do not "optimise" back.

## Layout & the one hard dependency rule

```
src/core/       scene, camera, timeline, transform, mesh, physics — NO GPU
src/platform/   sokol (window/input/gfx) wrapper + Renderer
src/ui/         Dear ImGui docking layout, theme, ImGuizmo wiring
src/formats/    PMX/VMD parsers — produce core data structures only
tests/          CTest executables for src/core (no GPU, no window)
cmake/          dependency fetching, warning flags, web shell
```

`core` must never include a sokol or ImGui header. Everything the tests touch
lives there, which is why `ctest` needs no display.

## Build / test / headless verify

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS=-g0 -DCMAKE_C_FLAGS=-g0
cmake --build build -j 1
ctest --test-dir build --output-on-failure
```

Warnings are not errors but the code must stay clean under `-Wall -Wextra
-Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wdouble-promotion
-Wformat=2`; third-party code is included SYSTEM so its warnings do not count.
`-Wdouble-promotion` fires on every `ImGui::Text*` varargs float (promoted to
double) — fix with an explicit `static_cast<double>(…)`, never by editing the
warning set.

Headless (no GPU) viewport verification:

```sh
Xvfb :77 -screen 0 1600x900x24 &
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
       MESA_GL_VERSION_OVERRIDE=4.5 MESA_GLSL_VERSION_OVERRIDE=450
DISPLAY=:77 ./build/MSentara &     # MSENTARA_FRAME_LIMIT=<n> self-quits
DISPLAY=:77 import -window root out.png
```

The only expected stderr noise is `LINUX_X11_QUERY_SYSTEM_DPI_FAILED`.

## sokol_gfx: the two traps that cost real time

Both were **silent in Release** (validation compiled out); both made the
offscreen viewport sample an empty texture while the ImGui UI looked fine.

1. **`sg_commit()` must be called once per frame**, after the final pass.
   `sokol_imgui.h` never calls it (grep: 0 hits). Without it the 3D viewport is
   black.
2. **Render-target `sg_image_desc` images inherit
   `sg_desc.environment.defaults.sample_count`** (the swapchain MSAA count)
   when left at 0. Pipelines built at 1 then trip
   `VALIDATE_APIP_ATTACHMENT_SAMPLE_COUNT`. Set `sample_count` explicitly on
   every offscreen colour/depth image.
3. **A transient buffer may be written only once per frame.** The offline
   exporter therefore renders one frame per `frame_cb` (no loop), with no
   `sg_commit()` between the skinning write and the draw — otherwise
   `VALIDATE_WRITEBUFFERTRANSIENT_WRITE_BEFORE_BIND` /
   `VALIDATE_DRAW_WRITE_BUFFER_TRANSIENT_MISSING`. Read pixels back *inside* the
   pass (`end_pass` unbinds the framebuffer).

**Rule: reproduce any sokol problem in a Debug build first.**

## Cross-cutting gotchas

- sokol_app X11 backend needs `X11` + `Xcursor`/`Xi`/`Xext` linked explicitly.
- sokol pulls GL headers only under `SOKOL_IMPL`; a TU that needs raw GL
  (`glReadPixels` in `Renderer.cpp`) must include `<GL/gl.h>` itself.
- VMD bone keys and camera keys use **different** interpolation-block layouts:
  bone = interleaved `(control*4+channel)`, camera = six sequential
  `(x0,x1,y0,y1)` curves. Never share a reader.
- MSVC's `<direct.h>` `mkdir` takes **one** argument (it is `_mkdir`) and has no
  `S_ISDIR`; wrap directory creation in a helper rather than calling `::mkdir`
  directly, or the Windows CI job fails to compile.
- `glm::perspective` puts -1 in `m[2][3]`, not `m[3][2]`.
- ImGuizmo must be included **after** `imgui.h`.
- Inside the viewport arbitrate input with `ImGuizmo::IsOver()/IsUsing()`, NOT
  `io.WantCaptureMouse` (always true there; would disable navigation).
- `R` is ambiguous: Blender `G/R/S` Rotate wins over DCC `W/E/R` Scale;
  `Shift+R` gives Scale.
- MMD is left-handed Y-up Z-forward: mirror Z (`vec3(x,y,-z)`) for right-handed
  GL, which also flips winding to what CCW pipelines want, so indices are used
  unchanged.
- PMX rigid bodies/joints and VMD layout details are in `docs/REFERENCES.md` —
  check it before touching a parser; do not infer layout from code.
- Emscripten path builds (`em++` available at `/tmp/emsdk`); shaders must be
  ES3-compatible (`Renderer.cpp` emits a `#version 410` / `#version 300 es`
  prologue; the ES `sampler2D` precision is required).
- **2D mode reuses the 3D `Camera`** — there is no separate 2D camera type.
  `projection_matrix_2d()` is an orthographic pixel-space projection where
  `distance` is the visible half-height (the 2D zoom) and `target.xy` is the
  pan; the view matrix is identity. Nav in 2D: middle-drag pans, wheel zooms
  (no orbit). Sprite quads use pixel orientation (y down, origin top-left),
  so `ImageIO::load_texture_file_2d` must **not** flip rows.
- **`Scene2D` mirrors `Scene`**: a flat, id-addressed `std::vector` with a
  `parent` index; `world_matrix()` recurses the parent chain. Like `Scene` it
  has **no cycle guard**, so a malformed project with an in-range parent cycle
  would recurse infinitely — keep them consistent if you ever fix one.
- The 2D gizmo manipulates `Scene2D::pivot_world_matrix()` (origin at the
  layer's pivot), so translation reads as `position` and rotate/scale act
  about the pivot — not the image corner.
- Headless 2D verify: `scripts/make_test_png.py` writes a stdlib-only RGBA PNG
  (red top / blue bottom, for orientation checks); CI renders it in 2D and
  asserts the exported frame count (`--export-start/--export-end` make the
  count deterministic). Export dirs `out-*/` are gitignored.
- **2D bone binding (M6B)**: `Layer2D::bound_bone` (a bone *name*) +
  `bind_offset` (vec2, in the bone's local frame). When the name resolves on
  the passed skeleton+pose, the bone's posed `global` **replaces** the layer's
  parent chain; otherwise it falls back to the parent chain (empty/unknown name
  is not an error). Threaded through `Scene2D::world_matrix(id, skel, pose)` /
  `pivot_world_matrix` / `pivot_base_matrix` (all default the pointers to
  nullptr, so existing 1-arg callers still compile). The gizmo disables editing
  for a bound layer (the bone drives it). Persisted in `ProjectLayer2D` as
  `bound_bone`/`bind_offset`, **written only when bound** so old files are
  byte-identical. 2D mode also drives the model's animation now (so bound bones
  follow the timeline); a bound ghost in onion skinning keeps the *current*
  pose (documented).
- **Onion skinning (M6B)**: pure maths in `src/core/OnionSkin.{hpp,cpp}`
  (`onion_skin_window` = far→near per side, clamped to the timeline, current
  frame excluded; `onion_ghost_tint` = past cool blue / future warm orange,
  alpha fading with distance). Settings live on the Editor; the app draws the
  ghosts in `render_scene_into_pass(true)` — the **export path passes `false`**
  so an offline sequence is the current frame alone.
- **Keyframe easing editor (M6B)**: one `draw_bezier_editor()` (anonymous ns in
  `Editor.cpp`) with preset buttons + an interactive 0..1 canvas (drag either
  interior control point; `Editor::easing_drag_handle_` holds the drag across
  frames). Works in 2D (selected layer's channel via `active_easing_curve()`)
  and 3D (selected bone's key: `easing_bone_channel_` 0..3 = X/Y/Z/Rotation,
  via the new non-const `ModelInstance::clip()` — guard with `find_bone` first
  because `AnimationClip::bone()` creates a track when missing).
