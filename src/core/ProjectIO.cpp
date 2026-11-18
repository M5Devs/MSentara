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
