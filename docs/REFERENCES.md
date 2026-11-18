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

## Fetching the sources

```sh
curl -sL "https://gist.githubusercontent.com/felixjones/f8a06bd48f9da9a4539f/raw/" \
     -o /tmp/pmx_spec.txt
curl -sL "https://raw.githubusercontent.com/benikabocha/saba/master/src/Saba/Model/MMD/PMXFile.cpp" \
     -o /tmp/saba_pmxfile.cpp
```

saba is MIT-licensed; the gist is a community specification. Neither is
vendored into this repository — only referenced.
