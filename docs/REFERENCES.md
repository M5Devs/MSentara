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

### PMX bones: append/grant and CCD-IK

The bone section is where the two hard parts of the rig live. After the tail
(the flags decide whether it is a `vec3` or a bone index), the sections
appear **only when their flag bit is set**, in this order:

| flag (u16) | bit | extra data |
| --- | --- | --- |
| `kBoneTailIsIndex` | `0x0001` | tail is a bone index, not a `vec3` |
| `kBoneIk` | `0x0020` | IK block (below) |
| `kBoneInheritRotation` | `0x0100` | append/grant parent index + weight |
| `kBoneInheritTranslation` | `0x0200` | (same two fields; rotation and translation share one parent+weight) |
| `kBoneFixedAxis` | `0x0400` | `vec3` axis |
| `kBoneLocalAxis` | `0x0800` | two `vec3` (local X, local Z) |
| `kBoneExternalParent` | `0x2000` | a **4-byte int**, despite being an "index" |

- **Append/Grant** ("inherit"): when either `0x0100` or `0x0200` is set, a bone
  index and a `float` weight follow. The bone copies that weight of its
  append-parent's **animated** rotation (slerp from identity) and/or
  translation. MMD composes `local = T * (ik * animate * append) * S`.
- **IK block**: `target` (bone index), `loopCount` (u32), `limitRadian`
  (float, the per-step clamp), `linkCount` (u32), then per link a bone index
  and a `u8`; when the `u8` is non-zero, two `vec3` limits follow. The limits
  are **signed radians**, and the Z-mirror negates both bounds (saba does
  `limit_min = -limit_max_raw`, `limit_max = -limit_min_raw`).
- **CCD-IK**: for each link (tip-first), rotate it so the effector→link vector
  lines up with the target→link vector. The axis is `cross(ikDir, targetDir)`
  so a positive angle turns the effector *toward* the target. The per-step
  clamp is `limitRadian` (MMD's default is `2*pi`, i.e. no clamp). An
  angle-limited link is clamped axis-by-axis in Euler XYZ.
- **The IK rotation lives on the effector *and* on every link**, not only on
  the bone that owns the solver. MMD gives each node its own `ikRotate`; the
  evaluation applies `Pose::ik_rotation` on all chain members
  (`Bone::ik_chain_member`).

### PMX morphs

A morph is `name_local`, `name_universal`, `panel` (u8), `type` (u8), then an
`i32` offset count and that many offsets. The offset layout depends on the
type:

| type | name | offset |
| --- | --- | --- |
| 0 | Group | morph index + `float` influence |
| 1 | Vertex | vertex index + `vec3` |
| 2 | Bone | bone index + `vec3` translation + `vec4` quaternion |
| 3–7 | UV / UV ext 1–4 | vertex index + `vec4` |
| 8 | Material | material index + `u8` op + diffuse/specular/shininess/ambient/edgeColor/edgeSize/tex/sphere/toon |
| 9 | Flip | morph index + `float` (same shape as a group entry) |
| 10 | Impulse | rigidbody index + `u8` local + two `vec3` |

MSentara evaluates **Vertex, Bone and Group** morphs; the others are skipped
by their exact byte size so a real file still parses. Group morphs are
flattened to leaf weights at evaluation time (with a visited set, so a cyclic
group cannot loop forever).

### PMX display frames, rigid bodies and joints

The sections after the bones come in this fixed order (2.1 adds soft bodies
last): **morphs → display frames → rigid bodies → joints**. The display frames
only describe the UI and are walked and discarded, but they must be walked
exactly or the rigid-body section is read from the wrong offset.

- **Display frame entry**: `type` (u8) then, for a bone frame (0), a bone
  index, or for a morph frame (1), a morph index. Nothing else.
- **Rigid body**: `name_local`, `name_universal`, related bone index, group
  (u8), non-collision mask (**u16**), shape (u8: 0 sphere, 1 box, 2 capsule),
  shape size (vec3), shape position (vec3), shape rotation (vec3 **radians**),
  mass, move attenuation, rotation damping, repulsion, friction (all f32),
  physics mode (u8: 0 follow-bone, 1 physics, 2 physics+bone).
- **Joint**: `name_local`, `name_universal`, type (u8), body index A, body
  index B, position (vec3), rotation (vec3 radians), position min/max (vec3),
  rotation min/max (vec3), position spring (vec3), rotation spring (vec3).
  Joint types 1–5 (6DOF, P2P, ConeTwist, Slider, Hinge) are **PMX 2.1**
  additions; a 2.0 file only ever has type 0 (Spring 6DOF).
- **Rigid-body and joint positions are model-space, not bone-local.** The body
  keeps a fixed offset from its bone that is derived at load time; only the Z
  mirror applies to the position and the rotation. Shape sizes are local
  extents and are left alone; joint limits are along the joint's own
  (already mirrored) axes and are kept as written.

## Fetching the sources

```sh
curl -sL "https://gist.githubusercontent.com/felixjones/f8a06bd48f9da9a4539f/raw/" \
     -o /tmp/pmx_spec.txt
curl -sL "https://raw.githubusercontent.com/benikabocha/saba/master/src/Saba/Model/MMD/PMXFile.cpp" \
     -o /tmp/saba_pmxfile.cpp
```

saba is MIT-licensed; the gist is a community specification. Neither is
vendored into this repository — only referenced.
