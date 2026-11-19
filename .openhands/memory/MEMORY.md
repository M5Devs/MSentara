# MSentara — project memory

Cross-platform 2D/3D animation studio (C++17 / CMake / sokol + Dear ImGui
docking + ImGuizmo + glm). Repo root `MSentara/`, GPLv3. Repo remote is
`M5Devs/MSentara`. Milestones 0–6C complete: skeleton+viewport, PMX loading +
skinning, VMD motion, morphs/append/CCD-IK, `.msentara` project format, CI,
PMX rigid-body physics (hair/cloth), VMD camera tracks + offline PNG
sequence export, a 2D cutout animation mode, **M6B: 2D bone binding + a
keyframe easing editor + onion skinning**, and **M6C: 2D sprite variants +
viewport layer picking + `.mschar2d` character presets**. **Per-milestone
detail lives in the daily logs** (e.g. `2026-11-17.md`); this index keeps only
durable, cross-cutting facts.

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
  `bind_offset` (vec2, bone-local). A resolvable name **replaces** the layer's
  parent chain with the bone's posed `global`; empty/unknown falls back (not an
  error). Threaded through `Scene2D::world_matrix/pivot_*` (pointers default
  nullptr). The gizmo disables editing for a bound layer. Persisted as
  `bound_bone`/`bind_offset`, written only when bound.
- **Onion skinning (M6B)**: pure maths in `src/core/OnionSkin.*`
  (`onion_skin_window` far→near, clamped, current excluded; `onion_ghost_tint`
  past blue / future orange with distance falloff). The app draws ghosts in
  `render_scene_into_pass(true)`; the **export path passes `false`**.
- **Keyframe easing editor (M6B)**: `draw_bezier_editor()` (Editor.cpp anon ns),
  2D (selected layer channel) + 3D (selected bone X/Y/Z/Rotation via the
  non-const `ModelInstance::clip()` — guard with `find_bone` first because
  `AnimationClip::bone()` creates a track when missing).

## Milestone 6C — 2D sprite variants, layer picking, character presets (v0.7.0)

Full detail in `2026-11-17.md` (M6C). Durable facts:
- **`LayerVariant{name, image_path, size}`** on `Layer2D` + `variants`/
  `active_variant`; `active_image_path()` and **`active_size()`** (variant size
  when known, else the layer's `size`) are the single source of truth.
  Out-of-range `active_variant` falls back to index 0 everywhere.
- **`active_size()` drives rendering and picking**: `layer_model_matrix`,
  `draw`, `draw_ghost_frame`, `pick_layer` all scale the unit quad by it, so a
  variant swap resizes the quad in the same frame. Onion ghosts must sample
  `texture_for(id, ghost_layer->active_variant)`, NOT `active_texture` (which
  reads the live layer).
- **`Scene2DInstance::ensure_texture_slots()`** resizes the texture vectors to
  `scene_.size()`. A preset instantiation appends layers straight to the core
  scene (bypassing `add_group`); without this self-heal, reload/upload reads out
  of bounds (it segfaulted the preset smoke).
- **`VariantTrack` is stepped**, not interpolated (holds at the key at/before
  the frame, jumps at each key) — unlike the Bezier tracks. Project key is
  `value_int`; `interp` is never written for it.
- **`Scene2D::pick_layer(world, skeleton, pose)`** iterates `draw_order()`
  reversed (top-most first) and inverts each layer's `world_matrix`.
  `Editor::pick_layer_at` maps the click through
  `Camera::screen_to_world_2d` (screen y down, clip y up → `v = 0.5 - clip.y/2`).
- **`.mschar2d` presets**: `Char2DPreset{name, layers}` (each layer a
  `ProjectLayer2D`), header `format:"msentara-char2d"`/`version:1` (mismatch
  rejected). `collect_char2d_preset` walks the subtree (cycle-guarded);
  `instantiate_char2d_preset` remaps parents and makes names unique
  (`head`→`head.2`).
- **Project format**: a plain single-image layer emits **neither** `variants`
  nor `active_variant` (byte-identical to M6); `layer2d_tracks` gain a stepped
  `variant` array of `value_int` keys.
- **CLI**: `--variant <layer>:<name>:<path>` (repeatable), `--preset <file>`;
  env `MSENTARA_PRESET` / `MSENTARA_VARIANT`.
- **Verify**: 12/12 ctest, `test_2d` 252 checks / 0 fail (107 new), gcc+clang+
  ASan+UBSan clean, `-g0 -j 1`; headless Xvfb+llvmpipe smokes (image+variant+
  export, preset import+variant+export, 3D export).

## Real-asset stress test (Task 15) — loaders verified against real MMD files

Real PMX/VMD assets live ONLY in `/tmp/real_assets/` (gdown; never in repo).
Verified with the full Miku model (27491 verts, 211 bones, 92 rigid bodies,
16 TGA/BMP/SPA textures) and a second model (2865 verts) × two motions:
- `ShiftJis.cpp` + generated `SjisTable.hpp` decode CP932 byte-perfectly (all
  65025 (lead,trail) pairs match Python cp932; 461 real VMD names identical).
- `resolve_texture_path` handles `\`, leading `\`, `.\`, `..\`, `C:\`, `C:/`,
  `/abs`, UTF-8 — all correct. All real textures resolve + decode.
- Bone-name matching is exact-string; a motion from a different rig matches only
  the shared primary skeleton (Miku×rapappa = 87/461, covering the whole body).
  NFKC normalization adds nothing — do NOT "fix" this, it is correct.
- Loaders + physics are referential-integrity clean and fuzz-clean (800
  truncations + 4000 corruptions under ASan, no crash/OOB/UB). PMX `bone=-1`
  on a rigid body is the legal "no bone" sentinel (`Physics::build` clamps it).
- Full load→morph→IK→physics→skin loop is ASan+UBSan clean on real data.

## Real-model bug fixes (commit 7ca6015) — durable rules

- **PMX triangle winding**: PMX stores **clockwise** (DirectX) front faces. The
  MMD→GL Z-mirror (`to_right_handed`, `vec3(x,y,-z)`) flips orientation, so the
  mirrored indices are CW and `SG_CULLMODE_BACK` culls the real front (model
  looks inside-out/dark). `PmxLoader::read_surfaces` must emit `v0,v2,v1`
  (CCW). Check via signed volume of the mirrored mesh (must be positive) or by
  a single-sided model's mean luminance before/after.
- **Skinned pipeline has no blend state** → PMX transparency is done by an
  **alpha cutout** in the skinned FS (`discard` when `diffuse.a*texel.a < 0.1`).
  Materials with the PMX "no-cull" flag get `SG_CULLMODE_NONE` (double-sided).
- **Viewport image needs a V flip on GL/GLES3** (`Editor::viewport_flips_v()`):
  the offscreen framebuffer origin is bottom-left but `ImGui::Image` maps the
  rect's top-left to `uv0`. The PNG exporter is separate and already flips its
  `glReadPixels` rows (`Renderer::read_color_pixels`).
- **Hair/strand physics**: a kinematic anchor teleports each frame; without
  carrying its per-frame delta into the hanging bodies first, the joint solver
  drags the strand across the jump in one substep and PBD velocity recovery
  launches it. `Physics::propagate_anchor_motion()` runs right after
  `integrate_kinematic` in `step`.
- **CJK names**: ImGui's default atlas is Latin-only; `load_ui_fonts()`
  (`src/ui/Theme.cpp`) merges a system CJK TTF. `AddFontFromFileTTF` **asserts**
  on a missing file, so guard the path with an existence check.
- **PMX names**: the *local* (JP) name is the canonical match key — MMD/VMD
  address bones & morphs by it. `Bone`/`MorphTarget` keep `name` (local) +
  `name_en` (universal) + `display_name()` (EN if present). UI labels use
  `display_name()`; every lookup/binding/ProjectIO uses `name`. Storing the
  universal name as `name` (an old bug) silently unbound every EN≠JP track
  (real Miku: morph tracks 1/104 → 36/104 after the fix).
- **PMX IK roles**: the "target index" is the **effector** (chain end that
  moves); the bone carrying the IK flag (e.g. `右足ＩＫ`) is the **goal**
  (controller). Inverting them makes a chain fly *away* from its handle and
  curl up (twin-tails bug). Matches `saba` `MMDIkSolver`. Bone-flag word is
  little-endian: `kBoneIk=0x0020` → bytes `0x20,0x00`.
- Export frames for offline inspection: `MSentara --pmx M --export DIR
  --export-start N --export-end N`; headless via Xvfb (`DISPLAY=:77`,
  `LIBGL_ALWAYS_SOFTWARE=1`) + `import -window root`.
