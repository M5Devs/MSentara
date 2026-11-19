// MSentara — cross-platform 2D/3D animation studio
// Copyright (C) 2026 The MSentara Authors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <string>
#include <vector>

#include "core/Animation.hpp"
#include "core/Layer2D.hpp"
#include "core/Math.hpp"

namespace ms {

// ---------------------------------------------------------------------------
// The native .msentara project format.
//
// A project file is plain, human-readable, versioned JSON. It never embeds
// the model or the motion — only the paths to them plus the editor state the
// user has authored on top (timeline, camera, IK toggle, morph weights and
// manual bone poses). That keeps it small, diffable and easy to hand-edit,
// and means a project reopens correctly after the model is re-exported.
//
// The magic `"format": "msentara-project"` and `"version"` let a future
// reader reject or migrate a file it does not understand.
// ---------------------------------------------------------------------------

inline constexpr const char* kProjectFormat = "msentara-project";
inline constexpr int kProjectVersion = 1;

// The camera state a project restores. Matches the fields of Camera that the
// user actually changes by navigating (the orbit parameters), so reopening a
// project returns to the exact viewpoint it was saved from.
struct ProjectCamera {
    vec3 target{0.0f, 0.0f, 0.0f};
    float distance = 8.0f;
    float yaw = -35.0f;
    float pitch = 25.0f;
    float roll = 0.0f;  // degrees; non-zero after a VMD camera keyframe
    float fov_y = 45.0f;
    bool orthographic = false;
};

// A manual morph weight override, keyed by the morph's name so it survives a
// model re-export that reorders morphs.
struct ProjectMorphWeight {
    std::string name;
    float weight = 0.0f;
};

// A manual bone pose override, keyed by the bone's name for the same reason.
// Values are the authored delta (a transform applied after the bone's rest
// transform), in the same units the property panel edits: world units and
// Euler XYZ degrees.
struct ProjectBonePose {
    std::string name;
    vec3 position{0.0f, 0.0f, 0.0f};
    vec3 rotation{0.0f, 0.0f, 0.0f};
    vec3 scale{1.0f, 1.0f, 1.0f};
};

// A serialised sprite variant of a 2D layer (an expression/state).
struct ProjectVariant2D {
    std::string name;
    std::string image_path;
};

// A serialised 2D layer. Mirrors Layer2D field for field; the parent is stored
// as an index into the project's `layers_2d` array, matching Scene2D.
struct ProjectLayer2D {
    std::string name;
    std::string image_path;
    // Optional sprite variants. Empty for a plain single-image layer, so an
    // older file (which has neither key) loads unchanged.
    std::vector<ProjectVariant2D> variants;
    int active_variant = 0;
    int parent = -1;
    vec2 pivot{0.0f, 0.0f};
    vec2 position{0.0f, 0.0f};
    float rotation = 0.0f;
    vec2 scale{1.0f, 1.0f};
    int z_order = 0;
    float opacity = 1.0f;
    vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
    bool visible = true;
    // Skeletal binding. Empty `bound_bone` (the default) means the layer is
    // driven by its own transform, so an older file that lacks these fields
    // loads unchanged.
    std::string bound_bone;
    vec2 bind_offset{0.0f, 0.0f};
};

// A serialised 2D keyframe channel. Only channels with keys are written, so a
// project that keys just one property stays small.
struct ProjectKey2D {
    int frame = 0;
    float value = 0.0f;    // scalar channels (rotation, opacity)
    vec2 value2{0.0f, 0.0f};  // vector channels (position, scale)
    int value_int = 0;        // discrete channel (variant index)
    // Interpolation curve that eases the span starting at this key. Stored as
    // four numbers; a linear curve is the default and is omitted on write.
    // Unused by the stepped variant channel.
    vec2 cp1{1.0f / 3.0f, 1.0f / 3.0f};
    vec2 cp2{2.0f / 3.0f, 2.0f / 3.0f};
};

// A serialised 2D layer track, keyed by layer name.
struct ProjectLayer2DTrack {
    std::string name;
    std::vector<ProjectKey2D> position;  // value2
    std::vector<ProjectKey2D> scale;     // value2
    std::vector<ProjectKey2D> rotation;  // value
    std::vector<ProjectKey2D> opacity;   // value
    std::vector<ProjectKey2D> variant;   // value_int (stepped)
};

// Everything a .msentara file stores, as plain data.
struct Project {
    std::string model_path;   // .pmx
    std::string motion_path;  // .vmd (may be empty)

    // Audio track the timeline plays and draws (may be empty). Stored as a
    // path like the model/motion, so the file stays small and readable.
    std::string audio_path;

    // Timeline / playback state.
    int frame_start = 1;
    int frame_end = 250;
    float fps = 24.0f;
    float current_frame = 1.0f;
    bool loop = true;

    ProjectCamera camera;

    bool ik_enabled = true;
    bool physics_enabled = true;
    // Whether the loaded motion's camera track drives the viewport camera.
    bool use_vmd_camera = false;

    std::vector<ProjectMorphWeight> morphs;
    std::vector<ProjectBonePose> bones;

    // 2D editor state. `editor_mode` is "3d" or "2d"; the layer list and its
    // tracks are empty for a 3D-only project, so an M4/M5 file loads with the
    // 3D defaults.
    std::string editor_mode = "3d";
    std::vector<ProjectLayer2D> layers_2d;
    std::vector<ProjectLayer2DTrack> layer2d_tracks;
};

// Serialises `project` to pretty-printed JSON text.
std::string project_to_json(const Project& project);

// Parses JSON text into `project`. On failure returns false and fills `error`
// with a human-readable reason (bad JSON, wrong format tag, unsupported
// version). A missing optional field takes its default, so a hand-written
// minimal file still loads.
bool project_from_json(const std::string& text, Project& project,
                       std::string& error);

// File helpers built on the two functions above. `error` is set on failure.
bool save_project(const std::string& path, const Project& project,
                  std::string& error);
bool load_project(const std::string& path, Project& project, std::string& error);

// ---------------------------------------------------------------------------
// The .mschar2d reusable 2D character preset.
//
// A lightweight JSON file holding a 2D root layer and its whole subtree
// (including variants and bound-bone names) so a cutout character can be
// dropped into any scene. It deliberately carries no animation tracks: a
// preset is a *rig*, and the tracks keyed by the layers' names belong to the
// project, not the character.
// ---------------------------------------------------------------------------

inline constexpr const char* kChar2DFormat = "msentara-char2d";
inline constexpr int kChar2DVersion = 1;

struct Char2DPreset {
    std::string name;
    // The subtree, root first. `parent` on each layer indexes into *this*
    // array (0-based); the root's parent is -1.
    std::vector<ProjectLayer2D> layers;
};

std::string char2d_to_json(const Char2DPreset& preset);
bool char2d_from_json(const std::string& text, Char2DPreset& preset,
                      std::string& error);
bool save_char2d(const std::string& path, const Char2DPreset& preset,
                 std::string& error);
bool load_char2d(const std::string& path, Char2DPreset& preset,
                 std::string& error);

// Gathers `root` and every descendant into a preset, rewriting parent indices
// to be relative to the collected subtree (so the preset is self-contained).
// Returns false for an invalid root. Variants and skeletal bindings are copied.
bool collect_char2d_preset(const Scene2D& scene, LayerId root,
                           const std::string& name, Char2DPreset& out);

// Appends a preset's layers to `scene`, giving every layer a *unique* name
// (so several characters can coexist in one scene and name-keyed tracks stay
// unambiguous) and resolving parent links within the new subtree. Returns the
// new root's id, or kInvalidLayer when the preset is empty.
LayerId instantiate_char2d_preset(Scene2D& scene, const Char2DPreset& preset);

}  // namespace ms
