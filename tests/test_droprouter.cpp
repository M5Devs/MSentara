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

// Tests for drag-and-drop routing: extension extraction and the
// extension -> loader mapping. Pure string work, so no window or GPU.

#include <string>
#include <vector>

#include "TestCommon.hpp"

#include "core/DropRouter.hpp"

using namespace ms;
using namespace ms::test;

namespace {

void test_extension_extraction() {
    MS_CHECK(drop_extension("model.pmx") == "pmx");
    MS_CHECK(drop_extension("assets/Model.PMX") == "pmx");
    MS_CHECK(drop_extension("a.b.c/file.VMD") == "vmd");
    MS_CHECK(drop_extension("C:\\rigs\\Miku.pmx") == "pmx");
    // A dot before the last separator is part of a directory name, not an
    // extension.
    MS_CHECK(drop_extension("v1.2/model") == "");
    MS_CHECK(drop_extension("noext") == "");
    MS_CHECK(drop_extension("trailingdot.") == "");
    MS_CHECK(drop_extension("") == "");
}

void test_classification() {
    MS_CHECK(classify_drop("m.pmx") == DropKind::Model);
    MS_CHECK(classify_drop("motion.vmd") == DropKind::Motion);
    MS_CHECK(classify_drop("head.png") == DropKind::Image);
    MS_CHECK(classify_drop("head.JPG") == DropKind::Image);
    MS_CHECK(classify_drop("tex.tga") == DropKind::Image);
    MS_CHECK(classify_drop("song.wav") == DropKind::Audio);
    MS_CHECK(classify_drop("song.MP3") == DropKind::Audio);
    MS_CHECK(classify_drop("song.flac") == DropKind::Audio);
    MS_CHECK(classify_drop("song.ogg") == DropKind::Audio);
    MS_CHECK(classify_drop("scene.msentara") == DropKind::Project);
    MS_CHECK(classify_drop("hero.mschar2d") == DropKind::Char2DPreset);
    MS_CHECK(classify_drop("notes.txt") == DropKind::Unknown);
    MS_CHECK(classify_drop("archive.zip") == DropKind::Unknown);
    MS_CHECK(classify_drop("noext") == DropKind::Unknown);
}

void test_loadable_and_names() {
    MS_CHECK(drop_kind_is_loadable(DropKind::Model));
    MS_CHECK(drop_kind_is_loadable(DropKind::Char2DPreset));
    MS_CHECK(!drop_kind_is_loadable(DropKind::Unknown));
    MS_CHECK(std::string(drop_kind_name(DropKind::Model)) == "model");
    MS_CHECK(std::string(drop_kind_name(DropKind::Unknown)) == "unknown");
}

void test_texture_extensions() {
    // Every image extension the router classifies as Image must also count as
    // a texture, so a dropped texture set travels with its model on the web.
    for (const char* ext : {"png", "jpg", "jpeg", "tga", "bmp", "dds", "spa",
                            "sph"}) {
        MS_CHECK_MSG(is_texture_extension(ext), ext);
    }
    MS_CHECK(!is_texture_extension("pmx"));
    MS_CHECK(!is_texture_extension("wav"));
    MS_CHECK(!is_texture_extension(""));
}

void test_plan_single_files() {
    // One of each kind maps to its own slot, in the order dropped.
    {
        const DropPlan p = plan_drop({"model.pmx"});
        MS_CHECK(p.model == 0);
        MS_CHECK(p.motion == -1);
        MS_CHECK(p.audio == -1);
        MS_CHECK(p.project == -1);
        MS_CHECK(!p.images_become_layers);  // no images at all
        MS_CHECK(p.ignored == 0);
    }
    {
        const DropPlan p = plan_drop({"a.vmd"});
        MS_CHECK(p.motion == 0);
        MS_CHECK(p.model == -1);
        MS_CHECK(p.ignored == 0);
    }
    {
        const DropPlan p = plan_drop({"song.wav"});
        MS_CHECK(p.audio == 0);
        MS_CHECK(p.ignored == 0);
    }
    // An empty drop is a valid, empty plan.
    {
        const DropPlan p = plan_drop({});
        MS_CHECK(p.model == -1);
        MS_CHECK(p.project == -1);
        MS_CHECK(p.ignored == 0);
        MS_CHECK(!p.images_become_layers);
    }
}

void test_plan_duplicates_keep_first() {
    // A second model/motion/audio cannot be shown, so it is ignored, and the
    // first one in drop order wins.
    const DropPlan p = plan_drop({"first.pmx", "second.pmx", "a.vmd", "b.vmd",
                                  "x.wav", "y.wav"});
    MS_CHECK(p.model == 0);
    MS_CHECK(p.motion == 2);
    MS_CHECK(p.audio == 4);
    MS_CHECK(p.ignored == 3);
}

void test_plan_project_supersedes() {
    // A project restores its own references, so nothing else in the drop is
    // loaded and every other file counts as ignored.
    const DropPlan p =
        plan_drop({"model.pmx", "song.wav", "scene.msentara", "extra.vmd"});
    MS_CHECK(p.project == 2);
    MS_CHECK(p.model == -1);
    MS_CHECK(p.motion == -1);
    MS_CHECK(p.audio == -1);
    MS_CHECK(!p.images_become_layers);
    MS_CHECK(p.ignored == 3);  // the three non-project files

    // A lone project ignores nothing.
    const DropPlan solo = plan_drop({"scene.msentara"});
    MS_CHECK(solo.project == 0);
    MS_CHECK(solo.ignored == 0);
}

void test_plan_images_become_layers_only_without_model() {
    // Images alone are 2D cutout layers.
    {
        const DropPlan p =
            plan_drop({"body.png", "face.png", "hair.jpg"});
        MS_CHECK(p.model == -1);
        MS_CHECK(p.images_become_layers);
        MS_CHECK(p.ignored == 0);  // all three become layers
    }
    // With a model, the same images are its textures: not layers, and counted
    // as ignored because the model loader resolves them itself.
    {
        const DropPlan p =
            plan_drop({"model.pmx", "body.png", "face.png"});
        MS_CHECK(p.model == 0);
        MS_CHECK(!p.images_become_layers);
        MS_CHECK(p.ignored == 2);
    }
}

void test_plan_presets_and_unknown() {
    // Every 2D preset is imported (not a single-slot kind) and none ignored.
    const DropPlan p = plan_drop(
        {"hero.mschar2d", "villain.mschar2d", "notes.txt"});
    MS_CHECK(p.ignored == 1);  // only the unknown file
    MS_CHECK(p.model == -1);

    // Unknown-only drops load nothing.
    const DropPlan junk = plan_drop({"readme.md", "archive.zip"});
    MS_CHECK(junk.ignored == 2);
    MS_CHECK(junk.model == -1);
    MS_CHECK(!junk.images_become_layers);
}

}  // namespace

int main() {
    test_extension_extraction();
    test_classification();
    test_loadable_and_names();
    test_texture_extensions();
    test_plan_single_files();
    test_plan_duplicates_keep_first();
    test_plan_project_supersedes();
    test_plan_images_become_layers_only_without_model();
    test_plan_presets_and_unknown();
    return summary("droprouter");
}
