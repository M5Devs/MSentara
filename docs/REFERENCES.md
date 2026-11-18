# PMX / VMD format references

Binary layouts for MikuMikuDance formats are easy to get subtly wrong, and a
wrong guess silently misaligns every field that follows. These are the
authoritative sources for this project. **Check them before changing any
parser code — do not infer a layout from the code alone.**

## PMX (Polygon Model eXtended) 2.0 / 2.1

- **Specification (English)** — felixjones, PMX 2.1 description
  <https://gist.githubusercontent.com/felixjones/f8a06bd48f9da9a4539f/raw/>
  The prose specification: index types, the globals header, deform types
  (BDEF1/2/4, SDEF, QDEF), and every section's field order. This is the
  primary source of truth.
- **Reference C++ implementation** — benikabocha/saba, `PMXFile.cpp`
  <https://raw.githubusercontent.com/benikabocha/saba/master/src/Saba/Model/MMD/PMXFile.cpp>
  A widely used reader. `ReadVertex` shows the exact per-vertex read order;
  useful when the spec's table wording is ambiguous.

## VMD (Vocaloid Motion Data)

- **Reference C++ implementation** — benikabocha/saba, `VMDFile.cpp`
  <https://raw.githubusercontent.com/benikabocha/saba/master/src/Saba/Model/MMD/VMDFile.cpp>
  The motion format: bone keyframes (name, frame, position, quaternion,
  interpolation), morph keyframes, camera and light keyframes.
- **Consumer** — benikabocha/saba, `VMDAnimation.cpp`
  <https://raw.githubusercontent.com/benikabocha/saba/master/src/Saba/Model/MMD/VMDAnimation.cpp>
  Shows how keyframes are sampled and interpolated in practice.

## Facts worth re-reading the sources for

These were each a real bug in this repository. Recorded here so they are not
re-guessed:

- **A vertex weight (`Weight 1`) in BDEF2 and SDEF is a 4-byte IEEE-754
  `float`**, not the 1-byte `[0,100]` value used by the older PMD format.
  BDEF4 and QDEF likewise use four `float` weights. Reading the wrong width
  desynchronises the entire remainder of the vertex section.
- **SDEF** is BDEF2 (two indices + one `float` weight) followed by three
  `vec3` values `C`, `R0`, `R1` (36 bytes), in that order.
- **`edgeScale` is a single `float` per vertex**, written immediately after
  that vertex's weights — it is *not* a trailing array of one value per
  vertex.
- **Index widths are per-category** and come from the globals header
  (vertex / texture / material / bone / morph / rigidbody). Vertex indices
  are unsigned; the others are signed so `-1` can mean "none".
- **Text** is a 4-byte signed length followed by that many bytes, UTF-16LE or
  UTF-8 per the encoding global.

### VMD specifics

- **Header** is `"Vocaloid Motion Data 0002"` in a 30-byte field, followed by
  a 20-byte Shift-JIS model name. The pre-`0002` variant
  (`"Vocaloid Motion Data file"`) uses a 10-byte model name.
- **A bone keyframe is 111 bytes**: `name[15]` (Shift-JIS, NUL-padded),
  `u32 frame`, `vec3 position`, `quat rotation` (stored **x, y, z, w**),
  then a **64-byte interpolation block**.
- **The interpolation block is four identical 16-byte copies** of the same
  values. Within a copy, byte `(control * 4 + channel)` holds that control,
  where control is `0=x0, 1=y0, 2=x1, 3=y1` and channel is
  `0=X, 1=Y, 2=Z, 3=rotation`. So the X-axis Bezier is built from bytes
  `0, 4, 8, 12`; the rotation curve from `3, 7, 11, 15`. Control points are
  the byte divided by 127. The four rows are interleaved by channel, **not**
  stored as four sequential 4-byte curves — reading it the latter way
  silently swaps X/Y/Z/rotation curves.
- **A morph keyframe is 23 bytes**: `name[15]`, `u32 frame`, `float weight`.
- **VMD is left-handed** (Y-up, Z-forward); PMX loading mirrors Z to reach
  the right-handed space MSentara renders in. The matching transform for a
  motion is translation `(x, y, -z)` and quaternion `(-x, -y, z, w)`
  (conjugate by `diag(1, 1, -1)`, which stays a proper rotation). A rotation
  about Z is unchanged by that mirror, which is a good sanity check.

## Fetching the sources

```sh
curl -sL "https://gist.githubusercontent.com/felixjones/f8a06bd48f9da9a4539f/raw/" \
     -o /tmp/pmx_spec.txt
curl -sL "https://raw.githubusercontent.com/benikabocha/saba/master/src/Saba/Model/MMD/PMXFile.cpp" \
     -o /tmp/saba_pmxfile.cpp
```

saba is MIT-licensed; the gist is a community specification. Neither is
vendored into this repository — only referenced.
