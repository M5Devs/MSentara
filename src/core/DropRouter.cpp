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

#include "core/DropRouter.hpp"

#include <cctype>

namespace ms {

std::string drop_extension(const std::string& path) {
    // The extension is the part after the last dot, but only when that dot is
    // after the last directory separator: "assets/v1.2/model" has no
    // extension, while "assets/model.PMX" has "pmx".
    const std::size_t sep = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep) ||
        dot + 1 >= path.size()) {
        return {};
    }
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

bool is_texture_extension(const std::string& extension) {
    return extension == "png" || extension == "jpg" || extension == "jpeg" ||
           extension == "tga" || extension == "bmp" || extension == "dds" ||
           extension == "spa" || extension == "sph";
}

DropKind classify_drop(const std::string& path) {
    const std::string ext = drop_extension(path);
    if (ext == "pmx") {
        return DropKind::Model;
    }
    if (ext == "vmd") {
        return DropKind::Motion;
    }
    if (is_texture_extension(ext)) {
        return DropKind::Image;
    }
    if (ext == "wav" || ext == "mp3" || ext == "flac" || ext == "ogg") {
        return DropKind::Audio;
    }
    if (ext == "msentara") {
        return DropKind::Project;
    }
    if (ext == "mschar2d") {
        return DropKind::Char2DPreset;
    }
    return DropKind::Unknown;
}

bool drop_kind_is_loadable(DropKind kind) {
    return kind != DropKind::Unknown;
}

const char* drop_kind_name(DropKind kind) {
    switch (kind) {
        case DropKind::Model:
            return "model";
        case DropKind::Motion:
            return "motion";
        case DropKind::Image:
            return "image";
        case DropKind::Audio:
            return "audio";
        case DropKind::Project:
            return "project";
        case DropKind::Char2DPreset:
            return "2D character preset";
        case DropKind::Unknown:
            break;
    }
    return "unknown";
}


DropPlan plan_drop(const std::vector<std::string>& paths) {
    DropPlan plan;
    int images = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const int index = static_cast<int>(i);
        switch (classify_drop(paths[i])) {
            case DropKind::Project:
                // A project names its own model, motion and audio, so it is the
                // whole answer: record the first and ignore the rest.
                if (plan.project < 0) {
                    plan.project = index;
                } else {
                    ++plan.ignored;
                }
                break;
            case DropKind::Model:
                if (plan.model < 0) {
                    plan.model = index;
                } else {
                    ++plan.ignored;
                }
                break;
            case DropKind::Motion:
                if (plan.motion < 0) {
                    plan.motion = index;
                } else {
                    ++plan.ignored;
                }
                break;
            case DropKind::Audio:
                if (plan.audio < 0) {
                    plan.audio = index;
                } else {
                    ++plan.ignored;
                }
                break;
            case DropKind::Image:
                ++images;
                break;
            case DropKind::Char2DPreset:
                break;  // every preset is imported; nothing is ignored
            case DropKind::Unknown:
                ++plan.ignored;
                break;
        }
    }

    if (plan.project >= 0) {
        // Nothing else in a project drop is used: the project restores its own
        // references, so every other file is ignored.
        plan.model = -1;
        plan.motion = -1;
        plan.audio = -1;
        plan.images_become_layers = false;
        plan.ignored = static_cast<int>(paths.size()) - 1;
        return plan;
    }

    // Images are textures when a model came with them (the model loader
    // resolves them relative to the model), and 2D layers otherwise. The flag
    // is only set when there is at least one image, so a drop with no images
    // never claims to create layers.
    plan.images_become_layers = (plan.model < 0) && (images > 0);
    if (!plan.images_become_layers) {
        plan.ignored += images;
    }
    return plan;
}

}  // namespace ms
