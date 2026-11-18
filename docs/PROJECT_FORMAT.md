# The `.msentara` project format

A `.msentara` file is the native project format. It is plain, human-readable,
versioned JSON. It stores **paths and editor state**, never the model or the
motion itself: the PMX model and the VMD motion stay in their own files, and
the project records where they are plus everything the user has authored on
top of them.

That keeps a project small and diffable, and means a project reopens correctly
after a model is re-exported (as long as the morph and bone names are stable).

## Top level

| Key | Type | Meaning |
| --- | --- | --- |
| `format` | string | Always `"msentara-project"`. Identifies the file. |
| `version` | int | Format version. This build writes and reads `1`. |
| `model` | string | Path to the `.pmx` model (may be empty). |
| `motion` | string | Path to the `.vmd` motion (may be empty). |
| `timeline` | object | Playback state, see below. |
| `camera` | object | Orbit-camera state, see below. |
| `ik_enabled` | bool | Whether CCD-IK solving is on. |
| `physics_enabled` | bool | Whether rigid-body simulation is on. Missing means `true`. |
| `use_vmd_camera` | bool | Whether the loaded motion's camera track drives the viewport. Missing means `false`. |
| `morphs` | array | Manual morph weights, see below. |
| `bones` | array | Manual bone poses, see below. |
| `editor_mode` | string | `"3d"` or `"2d"`. Which viewport the project reopens in. Missing means `"3d"`. |
| `layers_2d` | array | 2D cutout layers, see below. Empty for a 3D-only project. |
| `layer2d_tracks` | array | 2D layer animation tracks, see below. |

`format` and `version` are required. Every other key is optional and falls
back to a default, so a hand-written minimal file still loads:

```json
{ "format": "msentara-project", "version": 1 }
```

A file whose `format` is wrong, or whose `version` this build does not
understand, is rejected with a readable error rather than being misread.

## `timeline`

```json
{ "frame_start": 1, "frame_end": 250, "fps": 24.0,
  "current_frame": 1.0, "loop": true }
```

`loop` false means playback stops on the last frame instead of wrapping.

## `camera`

```json
{ "target": [0.0, 1.0, 0.0], "distance": 8.0,
  "yaw": -35.0, "pitch": 25.0,
  "roll": 0.0, "fov_y": 45.0, "orthographic": false }
```

`target` is a `[x, y, z]` triple; `yaw`, `pitch` and `roll` are in degrees.
These are the orbit parameters, so a reopened project returns to the exact
viewpoint it was saved from. `roll` becomes non-zero only when a VMD camera
keyframe with a tilt is applied; `fov_y` is the vertical field of view and
`orthographic` selects the projection. All three default to the interactive
camera's values, so a file written before they existed still loads.

## `morphs`

```json
[ { "name": "Smile", "weight": 0.5 } ]
```

Only morphs whose weight is non-zero are written. Entries are keyed by morph
**name**, so reordering morphs on re-export does not break the project. A name
that no longer exists is skipped on load.

## `bones`

```json
[ { "name": "root",
    "position": [0.0, 0.25, 0.0],
    "rotation": [0.0, 15.0, 0.0],
    "scale":    [1.0, 1.0, 1.0] } ]
```

Only bones whose delta differs from the rest pose are written. As with morphs,
entries are keyed by bone name and a missing name is skipped. `position` is in
world units, `rotation` is Euler XYZ degrees, `scale` is a multiplier.

## `layers_2d`

The 2D cutout layers, in paint-index order. Every field is written; only
`name` is required on load (the rest take their defaults). The skeletal
binding keys (`bound_bone`, `bind_offset`) are written only when a layer is
bound, so an unbound layer — and every project saved before Milestone 6B —
stays byte-identical.

```json
[ { "name": "head", "image": "assets/head.png",
    "parent": -1, "pivot": [128.0, 128.0], "position": [0.0, 0.0],
    "rotation": 0.0, "scale": [1.0, 1.0], "z_order": 0,
    "opacity": 1.0, "tint": [1.0, 1.0, 1.0, 1.0], "visible": true },
  { "name": "arm", "image": "assets/arm.png",
    "parent": 0, "pivot": [64.0, 32.0], "position": [60.0, 40.0],
    "rotation": 0.0, "scale": [1.0, 1.0], "z_order": 0,
    "opacity": 1.0, "tint": [1.0, 1.0, 1.0, 1.0], "visible": true,
    "bound_bone": "left_arm", "bind_offset": [0.0, -12.0] } ]
```

| Key | Type | Meaning |
| --- | --- | --- |
| `name` | string | Layer name. Also the key the tracks match on. |
| `image` | string | Path to the layer's image (PNG/JPG). Empty for a group node. |
| `parent` | int | Index into this array of the parent layer, or `-1` for a root. |
| `pivot` | `[x, y]` | Rotation/scale origin in image pixels (defaults to the image centre). |
| `position` | `[x, y]` | Where the pivot lands, in stage pixels. |
| `rotation` | float | Z rotation in degrees. |
| `scale` | `[x, y]` | Per-axis scale multiplier. |
| `z_order` | int | Paint order; lower draws behind. Ties keep insertion order. |
| `opacity` | float | 0..1, multiplied into the layer's alpha. |
| `tint` | `[r, g, b, a]` | Multiplied with the texel. |
| `visible` | bool | Hidden layers are skipped when drawing. |
| `bound_bone` | string | Name of a bone on the loaded model's rig this layer follows. Omitted when unbound. |
| `bind_offset` | `[x, y]` | Offset from the bone, in the bone's local frame. Omitted when zero. |

`parent` is a positional index, so a parent must appear **before** its children
in the array. The image path is stored verbatim, exactly like `model`/`motion`.

When `bound_bone` names a bone that exists on the model's skeleton, the layer's
parent chain is replaced by that bone's posed global transform (plus
`bind_offset`), so a cutout part follows the rig — including any CCD-IK that
posed it. An empty or unknown `bound_bone` leaves the layer on its parent chain
as before.

## `layer2d_tracks`

The 2D animation, one entry per animated layer, keyed by layer name. A layer
with no keys is not written.

```json
[ { "name": "arm",
    "position": [ { "frame": 0,  "value2": [60.0, 40.0] },
                  { "frame": 20, "value2": [180.0, 120.0],
                    "interp": { "cp1": [0.42, 0.0], "cp2": [1.0, 1.0] } } ],
    "scale":    [ { "frame": 0, "value2": [1.0, 1.0] } ],
    "rotation": [ { "frame": 0, "value": 0.0 } ],
    "opacity":  [ { "frame": 0, "value": 1.0 } ] } ]
```

`position` and `scale` keys carry a two-element `value2`; `rotation` and
`opacity` keys carry a scalar `value`. `interp` is the normalised Bezier
control points of the outgoing segment and is written only for a non-linear
key; its absence means linear (`cp1` `[1/3, 1/3]`, `cp2` `[2/3, 2/3]`). A
channel with no keys leaves that field at its authored value.

## Loading

A project can be opened from the File menu (type a path and press **Open
Project**) or from the command line:

```sh
./build/MSentara --project scene.msentara
```

A project supersedes `--pmx`/`--vmd`, since it names both files itself. Model
and motion paths are used verbatim, so either open the project from the
directory it was authored in or store absolute paths.

## Stability

The loader never throws on a well-formed JSON document: a field of the wrong
type takes its default instead of failing the whole load. Serialisation is
deterministic (fixed key order, two-space indent), so re-saving an unchanged
project produces a byte-identical file.
