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

#include "TestCommon.hpp"

#include <cstdio>
#include <string>

#include "core/ProjectIO.hpp"

using namespace ms;
using namespace ms::test;

namespace {

Project make_sample() {
    Project p;
    p.model_path = "models/hatsune_miku.pmx";
    p.motion_path = "motions/wave.vmd";
    p.frame_start = 3;
    p.frame_end = 180;
    p.fps = 30.0f;
    p.current_frame = 42.5f;
    p.loop = false;
    p.camera.target = vec3(1.5f, 2.0f, -3.25f);
    p.camera.distance = 12.75f;
    p.camera.yaw = -42.0f;
    p.camera.pitch = 17.5f;
    p.ik_enabled = false;
    p.physics_enabled = false;
    p.morphs = {{"Smile", 0.75f}, {"Blink", 0.25f}};
    p.bones = {
        {"center", vec3(0.0f, 1.0f, 2.0f), vec3(10.0f, 20.0f, 30.0f),
         vec3(1.0f, 1.0f, 1.0f)},
        {"head", vec3(-0.5f, 0.0f, 0.0f), vec3(0.0f, -15.0f, 5.0f),
         vec3(1.0f, 2.0f, 1.0f)},
    };
    return p;
}

void test_round_trip_preserves_everything() {
    const Project original = make_sample();
    const std::string text = project_to_json(original);
    MS_CHECK(!text.empty());

    Project loaded;
    std::string error;
    MS_CHECK_MSG(project_from_json(text, loaded, error), error);

    MS_CHECK(loaded.model_path == original.model_path);
    MS_CHECK(loaded.motion_path == original.motion_path);
    MS_CHECK(loaded.frame_start == original.frame_start);
    MS_CHECK(loaded.frame_end == original.frame_end);
    MS_CHECK(nearly(loaded.fps, original.fps));
    MS_CHECK(nearly(loaded.current_frame, original.current_frame));
    MS_CHECK(loaded.loop == original.loop);
    MS_CHECK(nearly(loaded.camera.target, original.camera.target));
    MS_CHECK(nearly(loaded.camera.distance, original.camera.distance));
    MS_CHECK(nearly(loaded.camera.yaw, original.camera.yaw));
    MS_CHECK(nearly(loaded.camera.pitch, original.camera.pitch));
    MS_CHECK(loaded.ik_enabled == original.ik_enabled);
    MS_CHECK(loaded.physics_enabled == original.physics_enabled);

    MS_CHECK(loaded.morphs.size() == original.morphs.size());
    if (loaded.morphs.size() == original.morphs.size()) {
        for (std::size_t i = 0; i < original.morphs.size(); ++i) {
            MS_CHECK(loaded.morphs[i].name == original.morphs[i].name);
            MS_CHECK(nearly(loaded.morphs[i].weight, original.morphs[i].weight));
        }
    }

    MS_CHECK(loaded.bones.size() == original.bones.size());
    if (loaded.bones.size() == original.bones.size()) {
        for (std::size_t i = 0; i < original.bones.size(); ++i) {
            MS_CHECK(loaded.bones[i].name == original.bones[i].name);
            MS_CHECK(nearly(loaded.bones[i].position,
                            original.bones[i].position));
            MS_CHECK(nearly(loaded.bones[i].rotation,
                            original.bones[i].rotation));
            MS_CHECK(nearly(loaded.bones[i].scale, original.bones[i].scale));
        }
    }

    // Re-serialising the parsed project must reproduce the exact same text, so
    // a save/load/save cycle is stable (no drift, no reordering).
    MS_CHECK(project_to_json(loaded) == text);
}

void test_header_fields_present() {
    const std::string text = project_to_json(make_sample());
    MS_CHECK(text.find("\"format\": \"msentara-project\"") != std::string::npos);
    MS_CHECK(text.find("\"version\": 1") != std::string::npos);
}

void test_rejects_wrong_format_tag() {
    Project loaded;
    std::string error;
    MS_CHECK(!project_from_json("{\"format\":\"something-else\",\"version\":1}",
                                loaded, error));
    MS_CHECK(!error.empty());
}

void test_rejects_unsupported_version() {
    Project loaded;
    std::string error;
    MS_CHECK(!project_from_json(
        "{\"format\":\"msentara-project\",\"version\":999}", loaded, error));
    MS_CHECK(!error.empty());
}

void test_rejects_malformed_json() {
    Project loaded;
    std::string error;
    MS_CHECK(!project_from_json("{ this is not json ", loaded, error));
    MS_CHECK(!error.empty());
}

void test_minimal_file_uses_defaults() {
    // Only the required header; every other field must fall back to a default.
    Project loaded;
    std::string error;
    MS_CHECK_MSG(project_from_json(
        "{\"format\":\"msentara-project\",\"version\":1}", loaded, error), error);
    MS_CHECK(loaded.model_path.empty());
    MS_CHECK(loaded.motion_path.empty());
    MS_CHECK(loaded.ik_enabled);
    // Absent from the file, so the backward-compatible default (on) applies.
    MS_CHECK(loaded.physics_enabled);
    MS_CHECK(loaded.loop);
    MS_CHECK(loaded.morphs.empty());
    MS_CHECK(loaded.bones.empty());
}

void test_file_round_trip() {
    const Project original = make_sample();
    const std::string path = "test_project_roundtrip.msentara";

    std::string error;
    MS_CHECK_MSG(save_project(path, original, error), error);

    Project loaded;
    MS_CHECK_MSG(load_project(path, loaded, error), error);
    MS_CHECK(loaded.model_path == original.model_path);
    MS_CHECK(loaded.morphs.size() == original.morphs.size());
    MS_CHECK(loaded.bones.size() == original.bones.size());

    std::remove(path.c_str());
}

void test_load_missing_file_fails() {
    Project loaded;
    std::string error;
    MS_CHECK(!load_project("does_not_exist_12345.msentara", loaded, error));
    MS_CHECK(!error.empty());
}

}  // namespace

int main() {
    test_round_trip_preserves_everything();
    test_header_fields_present();
    test_rejects_wrong_format_tag();
    test_rejects_unsupported_version();
    test_rejects_malformed_json();
    test_minimal_file_uses_defaults();
    test_file_round_trip();
    test_load_missing_file_fails();
    return summary("test_project");
}
