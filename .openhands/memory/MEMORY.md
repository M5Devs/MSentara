# MSentara — project memory

Cross-platform 2D/3D animation studio (C++17 / CMake / sokol + Dear ImGui
docking + ImGuizmo + glm). Repo root is `MSentara/`; `LICENSE` is GPLv3.
Milestone 0 (skeleton + viewport) is complete; Milestone 1 (PMX loading,
skeletal skinning, bone posing) is in progress. Daily logs hold the detail.

## Layout & the one hard dependency rule

```
src/core/       scene, camera, timeline, transform, mesh — NO rendering, NO GPU
src/platform/   sokol (window/input/gfx) wrapper + Renderer
src/ui/         Dear ImGui docking layout, theme, ImGuizmo wiring
src/formats/    PMX/VMD parsers — produce core data structures only
tests/          CTest executables for src/core (no GPU, no window)
cmake/          dependency fetching, warning flags, web shell
```

`core` must never include a sokol or ImGui header. Everything the tests touch
lives there, which is why `ctest` needs no display.

## sokol_gfx: the two traps that cost real time

Both were **silent in Release** (validation is compiled out) and both made the
offscreen viewport sample an empty texture while the ImGui UI looked fine.

1. **`sg_commit()` must be called once per frame**, after the final pass ends.
   sokol_gfx defers resource creation and buffer/image uploads to it;
   `sokol_imgui.h` never calls it (grep: 0 hits). Without it, ImGui geometry
   still draws (it is just draw-list data) but the 3D viewport is black.
2. **Render-target `sg_image_desc` images inherit
   `sg_desc.environment.defaults.sample_count`** (the swapchain MSAA count)
   when left at 0. Pipelines built at 1 then trip
   `VALIDATE_APIP_ATTACHMENT_SAMPLE_COUNT`. Set `sample_count` explicitly on
   every offscreen colour/depth image, and keep pipeline sample counts equal
   to the pass attachment's.

**Rule: reproduce any sokol problem in a Debug build first.** The validation
layer names the exact log item; Release tells you nothing.

## Headless verification (no GPU in this container)

Software rendering works and is how the viewport was visually confirmed:

```sh
Xvfb :77 -screen 0 1600x900x24 &
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
       MESA_GL_VERSION_OVERRIDE=4.5 MESA_GLSL_VERSION_OVERRIDE=450
DISPLAY=:77 ./build/MSentara &     # MSENTARA_FRAME_LIMIT=<n> self-quits
DISPLAY=:77 import -window root out.png
```

Judge the capture by counting distinct / non-background pixels per region, not
by eyeballing. The only expected stderr noise is
`LINUX_X11_QUERY_SYSTEM_DPI_FAILED` (no `Xft.dpi` under Xvfb).

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Deps via `FetchContent` (sokol, imgui `docking`, ImGuizmo, glm, stb).
ImGuizmo's own CMakeLists is bypassed with a non-existent `SOURCE_SUBDIR`.
Our code must stay clean under `-Wall -Wextra -Wpedantic -Wshadow
-Wconversion -Wsign-conversion -Wdouble-promotion -Wformat=2`; third-party
code is included as SYSTEM so its warnings do not count.

## Gotchas already hit

- sokol_app's X11 backend needs `X11` + `Xcursor`/`Xi`/`Xext` linked
  explicitly, else `undefined reference to 'XConvertSelection'`.
- `glm::perspective` puts -1 in `m[2][3]`, not `m[3][2]`.
- ImGuizmo must be included **after** `imgui.h`.
- Inside the viewport use `ImGuizmo::IsOver()/IsUsing()` to arbitrate input,
  **not** `io.WantCaptureMouse` — the viewport is itself an ImGui window, so
  that flag is always true there and would disable navigation entirely.
- `R` is ambiguous between Blender `G/R/S` (Rotate) and DCC `W/E/R` (Scale);
  Blender wins, `Shift+R` gives Scale.
- The Emscripten path is wired but **unverified** — no `emcc` in this
  container. WebGL2/GLES3 is the target, so shaders must be ES3-compatible
  (no UBO-free assumptions, no desktop-only GLSL).
