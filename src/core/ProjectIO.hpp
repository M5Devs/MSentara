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

// Everything a .msentara file stores, as plain data.
struct Project {
    std::string model_path;   // .pmx
    std::string motion_path;  // .vmd (may be empty)

    // Timeline / playback state.
    int frame_start = 1;
    int frame_end = 250;
    float fps = 24.0f;
    float current_frame = 1.0f;
    bool loop = true;

    ProjectCamera camera;

    bool ik_enabled = true;
    bool physics_enabled = true;

    std::vector<ProjectMorphWeight> morphs;
    std::vector<ProjectBonePose> bones;
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

}  // namespace ms
