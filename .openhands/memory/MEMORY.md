# MSentara — project memory

Cross-platform 2D/3D animation studio (C++17 / CMake / sokol + Dear ImGui
docking + ImGuizmo + glm). Repo root is `MSentara/`; `LICENSE` is GPLv3.
Milestone 0 (skeleton + viewport) complete. Milestone 1 (PMX loading, skeletal
skinning, bone posing) complete: parser + skinning + pose + bone-tree UI +
`--pmx` CLI, 7/7 ctest green. Daily logs hold the detail.

## Build memory limit — READ THIS FIRST

The container is killed by OOM during parallel C++ compilation. **Always
build with `-j 1`** (`cmake --build build -j 1`), never unbounded. Configure
with `-DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-g0 -DCMAKE_C_FLAGS=-g0` to
keep compiler RSS down. Run long builds in the background and poll the log;
foreground commands are capped ~1080 s.

The toolchain does **not** persist across container resets: `pip install
--user cmake ninja` and `sudo apt-get install -y libgl1-mesa-dev libglx-dev
libx11-dev libxcursor-dev libxi-dev libxext-dev libxrandr-dev libxinerama-dev
xvfb x11-utils imagemagick` must be re-run. `/workspace` persists.

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

## Milestone 1 — PMX parsing + skeletal skinning

- **`src/formats/PmxLoader.cpp`**: bounds-checked PMX 2.0/2.1 reader
  (`Reader` with `u8/i8/u16/i16/u32/i32/f32/vec*/text/index/skip/count`).
  Text is length-prefixed UTF-16LE or UTF-8 per the global flag.
- **PMX vertex weight types** (the layout trap that cost a full debug cycle):
  `0=BDEF1` (one index, implicit weight 1.0), `1=BDEF2` (two indices +
  **one-byte** weight in `[0,100]`, second bone gets `1 - w/100`),
  `2=BDEF4` (four indices + **four float** weights), `3=SDEF` (BDEF2 form
  plus 36 bytes of C/R0/R1 sphere params), `4=QDEF` (BDEF4 form). Each vertex
  then carries its **own edge-scale float** immediately after the weights —
  it is per-vertex, not a trailing array.
- **Handedness**: MMD is left-handed Y-up Z-forward; mirroring Z
  (`vec3(x, y, -z)`) converts to GL/GLM right-handed AND flips triangle
  winding, which is exactly what CCW pipelines want, so indices are used
  unchanged. Verified by a test that a vertex at z=+3 arrives at z=-3.
- **`src/core/Skeleton.cpp` `resolve_order`**: cycle-safe iterative DFS.
  A bone whose parent is already on the DFS stack must be emitted to break
  the cycle, else the loop never terminates (this was a real hang — the
  `test_deep_hierarchy_and_cycles` 2000-bone case exposed it).
- **CPU skinning** (`skin_mesh`) streams into a transient vertex buffer each
  frame, so no bone-matrix uniform array is needed (WebGL2 would choke on a
  full rig's worth). `src/ui/ModelInstance.cpp` owns model + pose + GPU
  resources and is the only place that ties core to the renderer.

### sokol revision pinned here (f5e8833) — API differences from older docs

- Textures use **separate** `sg_image` + `sg_view` (texture view) +
  `sg_sampler` objects, bound via `sg_bindings.views[i]` /
  `sg_bindings.samplers[i]`.
- Shader texture binding: `sg_shader_desc.views[i].texture.{stage,image_type,
  sample_type}` (note the nested `.texture`), `…samplers[i].{stage,
  sampler_type}`, and a `texture_sampler_pairs[i]` naming the GLSL sampler
  uniform (`glsl_name`) — required on GL 4.1 / WebGL2.
- Per-frame vertex data: create the buffer with `.usage.write_transient = true`
  and a fixed `.size`, then `sg_write_buffer_transient()` **outside** a pass,
  and draw it in the same frame. Growing it means destroy + recreate.
- `sg_write_buffer_unsealed` + `sg_seal_buffer` is the separate path for
  immutable one-shot uploads.
- `ImageIO.cpp` holds the single `STB_IMAGE_IMPLEMENTATION` (private include
  dir), normalises every source to RGBA8 with
  `stbi_set_flip_vertically_on_load(1)`, and a 1x1 white texture is the
  untextured-material fallback so the shader has no branch.

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
