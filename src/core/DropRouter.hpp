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

namespace ms {

// ---------------------------------------------------------------------------
// Drag-and-drop routing.
//
// A drop hands the app a flat list of paths; deciding what each one *is* is
// pure string work, so it lives in core where the unit tests can reach it
// without a window. The platform layer only supplies the paths (a native drop
// on desktop, a fetched file written into the Emscripten MEMFS on the web).
// ---------------------------------------------------------------------------

// What a dropped file should be loaded as, decided by its extension.
enum class DropKind {
    Unknown,     // not something we import; reported and skipped
    Model,       // .pmx — a 3D character
    Motion,      // .vmd — a motion / camera track
    Image,       // .png/.jpg/.tga/... — a 2D cutout layer or a texture
    Audio,       // .wav/.mp3/.flac/.ogg — a soundtrack
    Project,     // .msentara — a saved project
    Char2DPreset // .mschar2d — a reusable 2D character
};

// Lower-cases `path`'s extension, without the leading dot. Returns an empty
// string when the path has no extension (a dot before a directory separator
// does not count, so "../v1/model" has no extension).
std::string drop_extension(const std::string& path);

// The kind a single path maps to, by its extension.
DropKind classify_drop(const std::string& path);

// True when `kind` is something the app knows how to load (i.e. not Unknown).
bool drop_kind_is_loadable(DropKind kind);

// A short human-readable name for a kind, for status messages.
const char* drop_kind_name(DropKind kind);

// True when the extension is a texture image that a dropped model may refer to
// (PMX stores texture names relative to the model). Used to decide which extra
// dropped files travel alongside a model on the web, where they must all land
// in the same MEMFS directory for the relative references to resolve.
bool is_texture_extension(const std::string& extension);

// ---------------------------------------------------------------------------
// What a whole drop should do, decided without touching any loader.
//
// A drop is a flat list, but the app can only show one model and one motion at
// a time, and an image is either a 2D layer or a model texture depending on
// what else arrived. That policy is pure, so it lives here and is unit tested;
// the app just carries out the plan.
// ---------------------------------------------------------------------------
struct DropPlan {
    // Indices into the dropped path list, or -1 when the drop has none of that
    // kind. Only the first of each is kept: a second model would replace the
    // first, so loading it would be wasted work.
    int project = -1;   // set: it supersedes everything else
    int model = -1;
    int motion = -1;
    int audio = -1;

    // True when the drop contains at least one image and no model, so every
    // image is a 2D cutout layer. With a model present the images are presumed
    // to be that model's textures, left for the model loader to resolve, so
    // this stays false.
    bool images_become_layers = false;

    // Number of files that will be ignored: extra models/motions/audio, images
    // kept as textures, and anything unrecognised.
    int ignored = 0;
};

// Builds the plan for `paths` (in drop order).
DropPlan plan_drop(const std::vector<std::string>& paths);

}  // namespace ms
