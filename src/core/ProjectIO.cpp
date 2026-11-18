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

#include "core/ProjectIO.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

#include "nlohmann/json.hpp"

namespace ms {

namespace {

using json = nlohmann::json;

// A vec3 is written as a 3-element array rather than an object: it is shorter,
// reads like the numbers in the property panel, and is the conventional
// choice for a coordinate triple.
json vec3_to_json(const vec3& v) {
    return json::array({v.x, v.y, v.z});
}

// Reads a coordinate triple. Anything that is not an array of at least three
// numbers falls back to `fallback`, so a hand-edited file with a typo degrades
// to the default instead of throwing.
vec3 vec3_from_json(const json& value, const vec3& fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    if (!value[0].is_number() || !value[1].is_number() ||
        !value[2].is_number()) {
        return fallback;
    }
    return vec3(value[0].get<float>(), value[1].get<float>(),
                value[2].get<float>());
}

json vec2_to_json(const vec2& v) {
    return json::array({v.x, v.y});
}

vec2 vec2_from_json(const json& value, const vec2& fallback) {
    if (!value.is_array() || value.size() < 2) {
        return fallback;
    }
    if (!value[0].is_number() || !value[1].is_number()) {
        return fallback;
    }
    return vec2(value[0].get<float>(), value[1].get<float>());
}

json vec4_to_json(const vec4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}

vec4 vec4_from_json(const json& value, const vec4& fallback) {
    if (!value.is_array() || value.size() < 4) {
        return fallback;
    }
    for (int i = 0; i < 4; ++i) {
        if (!value[static_cast<std::size_t>(i)].is_number()) {
            return fallback;
        }
    }
    return vec4(value[0].get<float>(), value[1].get<float>(),
                value[2].get<float>(), value[3].get<float>());
}

// A default-constructed Bezier is the linear curve, whose control points sit at
// (1/3, 1/3) and (2/3, 2/3). A key using it is written without an
// interpolation block, which keeps a plain linear track compact.
bool bezier_is_linear(const Bezier& b) {
    const Bezier linear = Bezier::linear();
    return b.cp1 == linear.cp1 && b.cp2 == linear.cp2;
}

json bezier_to_json(const Bezier& b) {
    json out;
    out["cp1"] = vec2_to_json(b.cp1);
    out["cp2"] = vec2_to_json(b.cp2);
    return out;
}

Bezier bezier_from_json(const json& value) {
    Bezier b = Bezier::linear();
    if (!value.is_object()) {
        return b;
    }
    b.cp1 = vec2_from_json(value.value("cp1", json()), b.cp1);
    b.cp2 = vec2_from_json(value.value("cp2", json()), b.cp2);
    return b;
}

// Serialises one key. Scalar channels (rotation, opacity) store the value
// under "value"; vector channels (position, scale) under "value2".
json key_scalar_to_json(int frame, float value, const Bezier& b) {
    json entry;
    entry["frame"] = frame;
    entry["value"] = value;
    if (!bezier_is_linear(b)) {
        entry["interp"] = bezier_to_json(b);
    }
    return entry;
}

json key_vec2_to_json(int frame, const vec2& value, const Bezier& b) {
    json entry;
    entry["frame"] = frame;
    entry["value2"] = vec2_to_json(value);
    if (!bezier_is_linear(b)) {
        entry["interp"] = bezier_to_json(b);
    }
    return entry;
}

// Reads a key array into `out`. A non-array value leaves `out` empty (the
// channel is simply absent), matching how every other optional field behaves.
void keys_scalar_from_json(const json& value, std::vector<ProjectKey2D>& out) {
    if (!value.is_array()) {
        return;
    }
    for (const json& entry : value) {
        if (!entry.is_object()) {
            continue;
        }
        ProjectKey2D key;
        key.frame = entry.value("frame", 0);
        key.value = entry.value("value", 0.0f);
        const Bezier b = bezier_from_json(entry.value("interp", json()));
        key.cp1 = b.cp1;
        key.cp2 = b.cp2;
        out.push_back(key);
    }
}

void keys_vec2_from_json(const json& value, std::vector<ProjectKey2D>& out) {
    if (!value.is_array()) {
        return;
    }
    for (const json& entry : value) {
        if (!entry.is_object()) {
            continue;
        }
        ProjectKey2D key;
        key.frame = entry.value("frame", 0);
        key.value2 = vec2_from_json(entry.value("value2", json()), key.value2);
        const Bezier b = bezier_from_json(entry.value("interp", json()));
        key.cp1 = b.cp1;
        key.cp2 = b.cp2;
        out.push_back(key);
    }
}

json layer_2d_to_json(const ProjectLayer2D& layer) {
    json out;
    out["name"] = layer.name;
    out["image"] = layer.image_path;
    out["parent"] = layer.parent;
    out["pivot"] = vec2_to_json(layer.pivot);
    out["position"] = vec2_to_json(layer.position);
    out["rotation"] = layer.rotation;
    out["scale"] = vec2_to_json(layer.scale);
    out["z_order"] = layer.z_order;
    out["opacity"] = layer.opacity;
    out["tint"] = vec4_to_json(layer.tint);
    out["visible"] = layer.visible;
    // The skeletal binding is omitted when unused so an unbound layer stays
    // compact (and an older reader sees no new keys).
    if (!layer.bound_bone.empty()) {
        out["bound_bone"] = layer.bound_bone;
        if (layer.bind_offset != vec2(0.0f)) {
            out["bind_offset"] = vec2_to_json(layer.bind_offset);
        }
    }
    return out;
}

ProjectLayer2D layer_2d_from_json(const json& value) {
    ProjectLayer2D layer;
    if (!value.is_object()) {
        return layer;
    }
    layer.name = value.value("name", std::string());
    layer.image_path = value.value("image", std::string());
    layer.parent = value.value("parent", layer.parent);
    layer.pivot = vec2_from_json(value.value("pivot", json()), layer.pivot);
    layer.position =
        vec2_from_json(value.value("position", json()), layer.position);
    layer.rotation = value.value("rotation", layer.rotation);
    layer.scale = vec2_from_json(value.value("scale", json()), layer.scale);
    layer.z_order = value.value("z_order", layer.z_order);
    layer.opacity = value.value("opacity", layer.opacity);
    layer.tint = vec4_from_json(value.value("tint", json()), layer.tint);
    layer.visible = value.value("visible", layer.visible);
    // A missing binding defaults to empty (unbound), so an M6 file loads with
    // its layers driven by their own transforms exactly as before.
    layer.bound_bone = value.value("bound_bone", std::string());
    layer.bind_offset =
        vec2_from_json(value.value("bind_offset", json()), layer.bind_offset);
    return layer;
}

json layer_2d_track_to_json(const ProjectLayer2DTrack& track) {
    json out;
    out["name"] = track.name;
    const auto write_scalar = [&](const char* field,
                                  const std::vector<ProjectKey2D>& keys) {
        if (keys.empty()) {
            return;
        }
        json arr = json::array();
        for (const ProjectKey2D& k : keys) {
            Bezier b;
            b.cp1 = k.cp1;
            b.cp2 = k.cp2;
            arr.push_back(key_scalar_to_json(k.frame, k.value, b));
        }
        out[field] = arr;
    };
    const auto write_vec2 = [&](const char* field,
                                const std::vector<ProjectKey2D>& keys) {
        if (keys.empty()) {
            return;
        }
        json arr = json::array();
        for (const ProjectKey2D& k : keys) {
            Bezier b;
            b.cp1 = k.cp1;
            b.cp2 = k.cp2;
            arr.push_back(key_vec2_to_json(k.frame, k.value2, b));
        }
        out[field] = arr;
    };
    write_vec2("position", track.position);
    write_vec2("scale", track.scale);
    write_scalar("rotation", track.rotation);
    write_scalar("opacity", track.opacity);
    return out;
}

ProjectLayer2DTrack layer_2d_track_from_json(const json& value) {
    ProjectLayer2DTrack track;
    if (!value.is_object()) {
        return track;
    }
    track.name = value.value("name", std::string());
    keys_vec2_from_json(value.value("position", json()), track.position);
    keys_vec2_from_json(value.value("scale", json()), track.scale);
    keys_scalar_from_json(value.value("rotation", json()), track.rotation);
    keys_scalar_from_json(value.value("opacity", json()), track.opacity);
    return track;
}

json camera_to_json(const ProjectCamera& camera) {
    json out;
    out["target"] = vec3_to_json(camera.target);
    out["distance"] = camera.distance;
    out["yaw"] = camera.yaw;
    out["pitch"] = camera.pitch;
    out["roll"] = camera.roll;
    out["fov_y"] = camera.fov_y;
    out["orthographic"] = camera.orthographic;
    return out;
}

ProjectCamera camera_from_json(const json& value, const ProjectCamera& fallback) {
    if (!value.is_object()) {
        return fallback;
    }
    ProjectCamera camera = fallback;
    camera.target =
        vec3_from_json(value.value("target", json()), camera.target);
    camera.distance = value.value("distance", camera.distance);
    camera.yaw = value.value("yaw", camera.yaw);
    camera.pitch = value.value("pitch", camera.pitch);
    camera.roll = value.value("roll", camera.roll);
    camera.fov_y = value.value("fov_y", camera.fov_y);
    camera.orthographic = value.value("orthographic", camera.orthographic);
    return camera;
}

}  // namespace

std::string project_to_json(const Project& project) {
    json out;
    out["format"] = kProjectFormat;
    out["version"] = kProjectVersion;

    // Paths are stored exactly as given; resolving them relative to the
    // project file is the loader's job, so the file stays readable.
    out["model"] = project.model_path;
    out["motion"] = project.motion_path;

    json timeline;
    timeline["frame_start"] = project.frame_start;
    timeline["frame_end"] = project.frame_end;
    timeline["fps"] = project.fps;
    timeline["current_frame"] = project.current_frame;
    timeline["loop"] = project.loop;
    out["timeline"] = timeline;

    out["camera"] = camera_to_json(project.camera);
    out["ik_enabled"] = project.ik_enabled;
    out["physics_enabled"] = project.physics_enabled;
    out["use_vmd_camera"] = project.use_vmd_camera;
    out["editor_mode"] = project.editor_mode;

    json layers = json::array();
    for (const ProjectLayer2D& layer : project.layers_2d) {
        layers.push_back(layer_2d_to_json(layer));
    }
    out["layers_2d"] = layers;

    json tracks = json::array();
    for (const ProjectLayer2DTrack& track : project.layer2d_tracks) {
        tracks.push_back(layer_2d_track_to_json(track));
    }
    out["layer2d_tracks"] = tracks;

    json morphs = json::array();
    for (const ProjectMorphWeight& morph : project.morphs) {
        json entry;
        entry["name"] = morph.name;
        entry["weight"] = morph.weight;
        morphs.push_back(entry);
    }
    out["morphs"] = morphs;

    json bones = json::array();
    for (const ProjectBonePose& bone : project.bones) {
        json entry;
        entry["name"] = bone.name;
        entry["position"] = vec3_to_json(bone.position);
        entry["rotation"] = vec3_to_json(bone.rotation);
        entry["scale"] = vec3_to_json(bone.scale);
        bones.push_back(entry);
    }
    out["bones"] = bones;

    // Indent so the file diffs cleanly and can be read by a human.
    return out.dump(2) + "\n";
}

bool project_from_json(const std::string& text, Project& project,
                       std::string& error) {
    json in;
    try {
        in = json::parse(text);
    } catch (const json::parse_error& e) {
        error = std::string("invalid JSON: ") + e.what();
        return false;
    }

    if (!in.is_object()) {
        error = "project root is not a JSON object";
        return false;
    }
    if (in.value("format", std::string()) != kProjectFormat) {
        error = "not an msentara project (missing or wrong \"format\")";
        return false;
    }
    const int version = in.value("version", 0);
    if (version != kProjectVersion) {
        error = "unsupported project version " + std::to_string(version) +
                " (this build reads version " +
                std::to_string(kProjectVersion) + ")";
        return false;
    }

    // Start from a default-constructed Project so every field has a sane
    // value even when the file omits it.
    project = Project{};
    project.model_path = in.value("model", std::string());
    project.motion_path = in.value("motion", std::string());

    if (in.contains("timeline") && in["timeline"].is_object()) {
        const json& timeline = in["timeline"];
        project.frame_start = timeline.value("frame_start", project.frame_start);
        project.frame_end = timeline.value("frame_end", project.frame_end);
        project.fps = timeline.value("fps", project.fps);
        project.current_frame =
            timeline.value("current_frame", project.current_frame);
        project.loop = timeline.value("loop", project.loop);
    }

    project.camera = camera_from_json(in.value("camera", json()), project.camera);
    project.ik_enabled = in.value("ik_enabled", project.ik_enabled);
    // Absent in a project saved before physics existed: the default (true)
    // keeps those files loading unchanged.
    project.physics_enabled =
        in.value("physics_enabled", project.physics_enabled);
    project.use_vmd_camera =
        in.value("use_vmd_camera", project.use_vmd_camera);

    // Absent in a project saved before 2D existed: "3d" and empty layer lists
    // keep those files loading unchanged.
    project.editor_mode = in.value("editor_mode", project.editor_mode);
    if (project.editor_mode != "2d") {
        project.editor_mode = "3d";
    }

    if (in.contains("layers_2d") && in["layers_2d"].is_array()) {
        for (const json& entry : in["layers_2d"]) {
            if (!entry.is_object()) {
                continue;
            }
            project.layers_2d.push_back(layer_2d_from_json(entry));
        }
    }

    if (in.contains("layer2d_tracks") && in["layer2d_tracks"].is_array()) {
        for (const json& entry : in["layer2d_tracks"]) {
            if (!entry.is_object()) {
                continue;
            }
            ProjectLayer2DTrack track = layer_2d_track_from_json(entry);
            if (!track.name.empty()) {
                project.layer2d_tracks.push_back(std::move(track));
            }
        }
    }

    if (in.contains("morphs") && in["morphs"].is_array()) {
        for (const json& entry : in["morphs"]) {
            if (!entry.is_object()) {
                continue;
            }
            ProjectMorphWeight morph;
            morph.name = entry.value("name", std::string());
            morph.weight = entry.value("weight", 0.0f);
            if (!morph.name.empty()) {
                project.morphs.push_back(morph);
            }
        }
    }

    if (in.contains("bones") && in["bones"].is_array()) {
        for (const json& entry : in["bones"]) {
            if (!entry.is_object()) {
                continue;
            }
            ProjectBonePose bone;
            bone.name = entry.value("name", std::string());
            bone.position =
                vec3_from_json(entry.value("position", json()), bone.position);
            bone.rotation =
                vec3_from_json(entry.value("rotation", json()), bone.rotation);
            bone.scale = vec3_from_json(entry.value("scale", json()), bone.scale);
            if (!bone.name.empty()) {
                project.bones.push_back(bone);
            }
        }
    }

    return true;
}

bool save_project(const std::string& path, const Project& project,
                  std::string& error) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open '" + path + "' for writing";
        return false;
    }
    file << project_to_json(project);
    if (!file) {
        error = "failed while writing '" + path + "'";
        return false;
    }
    return true;
}

bool load_project(const std::string& path, Project& project,
                  std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open '" + path + "'";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return project_from_json(buffer.str(), project, error);
}

}  // namespace ms
