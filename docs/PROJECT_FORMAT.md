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
