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

#include <cmath>
#include <string>

#include "core/Animation.hpp"
#include "core/Layer2D.hpp"
#include "core/OnionSkin.hpp"
#include "core/ProjectIO.hpp"
#include "core/Skeleton.hpp"

using namespace ms;
using namespace ms::test;

namespace {

// --- Scene2D / Layer2D ----------------------------------------------------

void test_add_and_find() {
    Scene2D s;
    const LayerId a = s.add_layer("root");
    const LayerId b = s.add_layer("child", a);
    MS_CHECK(a == 0);
    MS_CHECK(b == 1);
    MS_CHECK(s.size() == 2);
    MS_CHECK(s.find(a) != nullptr);
    MS_CHECK(std::string(s.find(b)->name) == "child");
    MS_CHECK(s.find(b)->parent == a);
    MS_CHECK(s.find(9999) == nullptr);
    MS_CHECK(s.find(-1) == nullptr);
}

void test_parenting_to_missing_is_root() {
    Scene2D s;
    const LayerId a = s.add_layer("a", 42);
    MS_CHECK(s.find(a)->parent == kInvalidLayer);
}

void test_local_matrix_translates() {
    Layer2D layer;
    layer.position = vec2(10.0f, 20.0f);
    const mat4 m = layer.local_matrix();
    const vec4 p = m * vec4(0.0f, 0.0f, 0.0f, 1.0f);
    MS_CHECK_NEAR(vec2(p.x, p.y), vec2(10.0f, 20.0f));
}

void test_pivot_shifts_rotation_origin() {
    // A 100x100 layer with the pivot at its centre. Rotating 90 degrees must
    // leave the centre fixed: the pivot is the rotation origin.
    Layer2D layer;
    layer.pivot = vec2(50.0f, 50.0f);
    layer.position = vec2(0.0f, 0.0f);
    layer.rotation = 90.0f;

    // The layer's local origin (its top-left) is at (-50, 50) after a 90-degree
    // rotation about the centre: a quarter turn of (-50, -50) is (50, -50),
    // shifted by the pivot offset.
    const vec4 origin = layer.local_matrix() * vec4(0.0f, 0.0f, 0.0f, 1.0f);
    MS_CHECK(nearly(vec2(origin.x, origin.y), vec2(50.0f, -50.0f), 1e-3f));
}

void test_world_matrix_composes_parent() {
    Scene2D s;
    const LayerId parent = s.add_layer("parent");
    s.find(parent)->position = vec2(100.0f, 0.0f);
    const LayerId child = s.add_layer("child", parent);
    s.find(child)->position = vec2(10.0f, 5.0f);

    // With no rotation, the child's world position is the sum of the chain.
    const mat4 w = s.world_matrix(child);
    const vec4 p = w * vec4(0.0f, 0.0f, 0.0f, 1.0f);
    MS_CHECK_NEAR(vec2(p.x, p.y), vec2(110.0f, 5.0f));
}

void test_parent_rotation_rotates_child() {
    Scene2D s;
    const LayerId parent = s.add_layer("parent");
    s.find(parent)->rotation = 90.0f;
    const LayerId child = s.add_layer("child", parent);
    s.find(child)->position = vec2(100.0f, 0.0f);

    // The parent's 90-degree turn rotates the child's +X offset onto +Y (with
    // y pointing down in pixel space, a +90 CCW turn maps (100,0) to (0,100)
    // in math terms; the matrix is a standard rotation, so verify numerically).
    const mat4 w = s.world_matrix(child);
    const vec4 p = w * vec4(0.0f, 0.0f, 0.0f, 1.0f);
    MS_CHECK(nearly(vec2(p.x, p.y), vec2(0.0f, 100.0f), 1e-3f));
}

void test_world_pivot() {
    Scene2D s;
    const LayerId a = s.add_layer("a");
    s.find(a)->position = vec2(20.0f, 30.0f);
    s.find(a)->pivot = vec2(5.0f, 5.0f);
    // `position` is where the layer's pivot lands, so the world pivot is the
    // position itself (the pivot is the origin the transform is built around).
    MS_CHECK_NEAR(s.world_pivot(a), vec2(20.0f, 30.0f));

    // The pivot is the point the image rotates about: rotating 90 degrees about
    // it leaves the world pivot unchanged.
    s.find(a)->rotation = 90.0f;
    MS_CHECK_NEAR(s.world_pivot(a), vec2(20.0f, 30.0f));
}

void test_draw_order_sorts_by_z_stable() {
    Scene2D s;
    const LayerId a = s.add_layer("a");  // z 0
    const LayerId b = s.add_layer("b");  // z 0
    const LayerId c = s.add_layer("c");  // z -1 (behind)
    s.find(c)->z_order = -1;
    const LayerId d = s.add_layer("d");  // z 5 (front)
    s.find(d)->z_order = 5;

    const std::vector<LayerId> order = s.draw_order();
    MS_CHECK(order.size() == 4);
    // c first (lowest z), then a and b in insertion order (stable), then d.
    MS_CHECK(order[0] == c);
    MS_CHECK(order[1] == a);
    MS_CHECK(order[2] == b);
    MS_CHECK(order[3] == d);
}

void test_selection() {
    Scene2D s;
    const LayerId a = s.add_layer("a");
    s.select(a);
    MS_CHECK(s.selected() == a);
    MS_CHECK(s.selected_layer() != nullptr);
    s.select(9999);
    MS_CHECK(s.selected() == kInvalidLayer);
    MS_CHECK(s.selected_layer() == nullptr);
}

// --- 2D tracks ------------------------------------------------------------

void test_vec2_track_linear_interpolation() {
    Vec2Track track;
    track.set_key(0, vec2(0.0f, 0.0f));
    track.set_key(10, vec2(100.0f, 50.0f));
    MS_CHECK(track.keys.size() == 2);
    MS_CHECK_NEAR(track.sample(5.0f), vec2(50.0f, 25.0f));
}

void test_vec2_track_holds_outside_range() {
    Vec2Track track;
    track.set_key(5, vec2(1.0f, 2.0f));
    track.set_key(10, vec2(3.0f, 4.0f));
    MS_CHECK_NEAR(track.sample(0.0f), vec2(1.0f, 2.0f));
    MS_CHECK_NEAR(track.sample(99.0f), vec2(3.0f, 4.0f));
}

void test_vec2_track_set_key_replaces() {
    Vec2Track track;
    track.set_key(3, vec2(1.0f, 1.0f));
    track.set_key(3, vec2(9.0f, 9.0f));
    MS_CHECK(track.keys.size() == 1);
    MS_CHECK_NEAR(track.sample(3.0f), vec2(9.0f, 9.0f));
    MS_CHECK(track.has_key(3));
    MS_CHECK(track.remove_key(3));
    MS_CHECK(track.empty());
    MS_CHECK(!track.remove_key(3));
}

void test_vec2_track_keeps_sorted_on_out_of_order_insert() {
    Vec2Track track;
    track.set_key(10, vec2(10.0f, 0.0f));
    track.set_key(0, vec2(0.0f, 0.0f));
    track.set_key(5, vec2(5.0f, 0.0f));
    MS_CHECK(track.keys.size() == 3);
    MS_CHECK(track.keys[0].frame == 0);
    MS_CHECK(track.keys[1].frame == 5);
    MS_CHECK(track.keys[2].frame == 10);
    MS_CHECK(track.frame_start() == 0);
    MS_CHECK(track.frame_end() == 10);
}

void test_vec2_track_bezier_easing() {
    // An ease-in curve makes the midpoint lag behind the linear one.
    Vec2Track track;
    Bezier ease_in;
    ease_in.cp1 = vec2(0.42f, 0.0f);
    ease_in.cp2 = vec2(1.0f, 1.0f);
    track.set_key(0, vec2(0.0f, 0.0f), ease_in);
    track.set_key(10, vec2(100.0f, 0.0f));
    const float mid = track.sample(5.0f).x;
    MS_CHECK(mid > 0.0f);
    MS_CHECK(mid < 50.0f);
}

void test_float_track_linear() {
    FloatTrack track;
    track.set_key(0, 0.0f);
    track.set_key(4, 8.0f);
    MS_CHECK_NEAR(track.sample(2.0f), 4.0f);
    MS_CHECK_NEAR(track.sample(-1.0f), 0.0f);
    MS_CHECK_NEAR(track.sample(100.0f), 8.0f);
}

void test_layer_2d_track_samples_only_keyed_channels() {
    Layer2DTrack track;
    track.name = "head";
    track.rotation.set_key(0, 0.0f);
    track.rotation.set_key(10, 90.0f);

    // The base carries the authored position/scale; only rotation is keyed, so
    // the other fields must come back unchanged.
    Layer2DTrackState base;
    base.position = vec2(7.0f, 8.0f);
    base.scale = vec2(2.0f, 3.0f);
    base.opacity = 0.5f;

    const Layer2DTrackState mid = track.sample(5.0f, base);
    MS_CHECK_NEAR(mid.rotation, 45.0f);
    MS_CHECK_NEAR(mid.position, base.position);
    MS_CHECK_NEAR(mid.scale, base.scale);
    MS_CHECK_NEAR(mid.opacity, base.opacity);
}

void test_layer_2d_track_empty() {
    Layer2DTrack track;
    MS_CHECK(track.empty());
    track.opacity.set_key(0, 1.0f);
    MS_CHECK(!track.empty());
    MS_CHECK(track.frame_end() == 0);
}

void test_apply_layer_2d_clip() {
    Scene2D scene;
    const LayerId head = scene.add_layer("head");
    const LayerId torso = scene.add_layer("torso");
    scene.find(head)->position = vec2(0.0f, 0.0f);

    AnimationClip clip;
    Layer2DTrack& track = clip.layer_2d("head");
    track.position.set_key(0, vec2(0.0f, 0.0f));
    track.position.set_key(10, vec2(100.0f, 0.0f));
    track.opacity.set_key(0, 1.0f);
    track.opacity.set_key(10, 0.0f);
    MS_CHECK(clip.has_layer_2d());

    MS_CHECK(apply_layer_2d_clip(clip, 5.0f, scene));
    MS_CHECK_NEAR(scene.find(head)->position, vec2(50.0f, 0.0f));
    MS_CHECK_NEAR(scene.find(head)->opacity, 0.5f);
    // A layer with no track is untouched.
    MS_CHECK_NEAR(scene.find(torso)->position, vec2(0.0f, 0.0f));

    // An unmatched track drives nothing.
    AnimationClip other;
    other.layer_2d("ghost").position.set_key(0, vec2(1.0f, 1.0f));
    MS_CHECK(!apply_layer_2d_clip(other, 0.0f, scene));
}

void test_clip_frame_end_includes_2d() {
    AnimationClip clip;
    MS_CHECK(clip.empty());
    MS_CHECK(clip.frame_end() == 0);
    clip.layer_2d("a").position.set_key(25, vec2(1.0f, 1.0f));
    MS_CHECK(!clip.empty());
    MS_CHECK(clip.frame_end() == 25);
}

// --- ProjectIO round-trip -------------------------------------------------

void test_project_round_trip_2d() {
    Project p;
    p.editor_mode = "2d";

    ProjectLayer2D root;
    root.name = "root";
    root.z_order = -1;
    root.position = vec2(10.0f, 20.0f);
    root.scale = vec2(1.5f, 0.5f);
    root.tint = vec4(1.0f, 0.5f, 0.25f, 0.75f);
    p.layers_2d.push_back(root);

    ProjectLayer2D child;
    child.name = "arm";
    child.image_path = "assets/arm.png";
    child.parent = 0;
    child.pivot = vec2(12.0f, 34.0f);
    child.rotation = 45.0f;
    child.opacity = 0.8f;
    child.visible = false;
    p.layers_2d.push_back(child);

    ProjectLayer2DTrack track;
    track.name = "arm";
    ProjectKey2D k0;
    k0.frame = 0;
    k0.value2 = vec2(0.0f, 0.0f);
    ProjectKey2D k1;
    k1.frame = 30;
    k1.value2 = vec2(50.0f, 10.0f);
    k1.cp1 = vec2(0.42f, 0.0f);
    k1.cp2 = vec2(1.0f, 1.0f);
    track.position = {k0, k1};
    ProjectKey2D r0;
    r0.frame = 0;
    r0.value = 5.0f;
    track.rotation = {r0};
    p.layer2d_tracks.push_back(track);

    const std::string text = project_to_json(p);
    Project loaded;
    std::string error;
    MS_CHECK_MSG(project_from_json(text, loaded, error), error);

    MS_CHECK(loaded.editor_mode == "2d");
    MS_CHECK(loaded.layers_2d.size() == 2);
    MS_CHECK(loaded.layers_2d[0].name == "root");
    MS_CHECK(loaded.layers_2d[0].z_order == -1);
    MS_CHECK_NEAR(loaded.layers_2d[0].position, vec2(10.0f, 20.0f));
    MS_CHECK_NEAR(loaded.layers_2d[0].scale, vec2(1.5f, 0.5f));
    MS_CHECK_NEAR(loaded.layers_2d[0].tint, vec4(1.0f, 0.5f, 0.25f, 0.75f));
    MS_CHECK(loaded.layers_2d[1].image_path == "assets/arm.png");
    MS_CHECK(loaded.layers_2d[1].parent == 0);
    MS_CHECK_NEAR(loaded.layers_2d[1].pivot, vec2(12.0f, 34.0f));
    MS_CHECK_NEAR(loaded.layers_2d[1].rotation, 45.0f);
    MS_CHECK_NEAR(loaded.layers_2d[1].opacity, 0.8f);
    MS_CHECK(loaded.layers_2d[1].visible == false);

    MS_CHECK(loaded.layer2d_tracks.size() == 1);
    MS_CHECK(loaded.layer2d_tracks[0].name == "arm");
    MS_CHECK(loaded.layer2d_tracks[0].position.size() == 2);
    MS_CHECK(loaded.layer2d_tracks[0].position[1].frame == 30);
    MS_CHECK_NEAR(loaded.layer2d_tracks[0].position[1].value2,
                  vec2(50.0f, 10.0f));
    // A non-linear key keeps its curve; a linear one omits it (and so reloads
    // as the default linear Bezier).
    MS_CHECK_NEAR(loaded.layer2d_tracks[0].position[1].cp1, vec2(0.42f, 0.0f));
    MS_CHECK_NEAR(loaded.layer2d_tracks[0].position[0].cp1,
                  vec2(1.0f / 3.0f, 1.0f / 3.0f));
    MS_CHECK(loaded.layer2d_tracks[0].rotation.size() == 1);
    MS_CHECK_NEAR(loaded.layer2d_tracks[0].rotation[0].value, 5.0f);
}

void test_project_without_2d_defaults_to_3d() {
    // A project written before 2D existed has neither the mode nor the layer
    // arrays; it must still load, in 3D mode, with empty 2D state.
    const std::string text = R"({
        "format": "msentara-project",
        "version": 1,
        "model": "m.pmx"
    })";
    Project loaded;
    std::string error;
    MS_CHECK_MSG(project_from_json(text, loaded, error), error);
    MS_CHECK(loaded.editor_mode == "3d");
    MS_CHECK(loaded.layers_2d.empty());
    MS_CHECK(loaded.layer2d_tracks.empty());
}

void test_project_2d_json_key_names() {
    // Locks the on-disk key names the docs promise: vector keys under "value2"
    // and a non-linear key's curve under "interp".
    Project p;
    p.editor_mode = "2d";
    ProjectLayer2D layer;
    layer.name = "arm";
    layer.image_path = "arm.png";
    p.layers_2d.push_back(layer);

    ProjectLayer2DTrack track;
    track.name = "arm";
    ProjectKey2D vk;
    vk.frame = 4;
    vk.value2 = vec2(3.0f, 9.0f);
    track.position = {vk};
    ProjectKey2D sk;
    sk.frame = 4;
    sk.value = 2.5f;
    sk.cp1 = vec2(0.1f, 0.2f);
    sk.cp2 = vec2(0.8f, 0.9f);
    track.rotation = {sk};
    p.layer2d_tracks.push_back(track);

    const std::string text = project_to_json(p);
    MS_CHECK(text.find("\"value2\"") != std::string::npos);
    MS_CHECK(text.find("\"interp\"") != std::string::npos);
    MS_CHECK(text.find("\"layers_2d\"") != std::string::npos);
    MS_CHECK(text.find("\"layer2d_tracks\"") != std::string::npos);
    MS_CHECK(text.find("\"editor_mode\"") != std::string::npos);

    // A linear key must not emit an "interp" block.
    Project lin;
    lin.editor_mode = "2d";
    ProjectLayer2DTrack lt;
    lt.name = "arm";
    ProjectKey2D lk;
    lk.frame = 0;
    lk.value2 = vec2(1.0f, 1.0f);
    lt.position = {lk};
    lin.layer2d_tracks.push_back(lt);
    const std::string lin_text = project_to_json(lin);
    MS_CHECK(lin_text.find("\"interp\"") == std::string::npos);
}

void test_project_rejects_bad_editor_mode() {
    Project p;
    p.editor_mode = "2d";
    const std::string text = project_to_json(p);
    // Overwrite the mode with nonsense; the loader must coerce it back to 3d.
    std::string patched = text;
    const std::string needle = "\"editor_mode\": \"2d\"";
    const std::size_t at = patched.find(needle);
    MS_CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        patched.replace(at, needle.size(), "\"editor_mode\": \"bogus\"");
    }
    Project loaded;
    std::string error;
    MS_CHECK_MSG(project_from_json(patched, loaded, error), error);
    MS_CHECK(loaded.editor_mode == "3d");
}

// --- Camera 2D projection -------------------------------------------------

void test_projection_2d_maps_pixels_to_clip() {
    Camera cam;
    cam.target = vec3(0.0f, 0.0f, 0.0f);
    cam.distance = 100.0f;  // half-height of 100 px
    const mat4 proj = cam.projection_matrix_2d(1.0f);

    // The view centre (the target) maps to clip origin.
    const vec4 centre = proj * vec4(0.0f, 0.0f, 0.0f, 1.0f);
    MS_CHECK(nearly(vec2(centre.x, centre.y), vec2(0.0f, 0.0f), 1e-3f));

    // y increases downward: a point 100 px below the centre lands at clip y = -1
    // (the bottom of the NDC cube).
    const vec4 bottom = proj * vec4(0.0f, 100.0f, 0.0f, 1.0f);
    MS_CHECK(nearly(bottom.y, -1.0f, 1e-3f));

    // A point 100 px right of the centre lands at clip x = +1.
    const vec4 right = proj * vec4(100.0f, 0.0f, 0.0f, 1.0f);
    MS_CHECK(nearly(right.x, 1.0f, 1e-3f));

    // Panning the target moves the visible region with it.
    cam.target = vec3(50.0f, 0.0f, 0.0f);
    const mat4 panned = cam.projection_matrix_2d(1.0f);
    const vec4 shifted = panned * vec4(50.0f, 0.0f, 0.0f, 1.0f);
    MS_CHECK(nearly(shifted.x, 0.0f, 1e-3f));
}

// --- M6B: bone binding ----------------------------------------------------

// Builds a two-bone rig (root at the origin, child offset +100 on X) and
// evaluates it, returning the skeleton and pose. The pose is a fresh rest pose.
void build_two_bone_rig(Skeleton& skeleton, Pose& pose) {
    skeleton.add_bone("root", -1, vec3(0.0f, 0.0f, 0.0f));
    skeleton.add_bone("arm", 0, vec3(100.0f, 0.0f, 0.0f));
    skeleton.compute_rest();
    pose.resize(skeleton.size());
    evaluate_pose(skeleton, pose);
}

void test_bone_binding_overrides_parent() {
    // A layer bound to a bone ignores its parent chain: the bone's global
    // replaces the parent's world matrix. Bind the child to a bone whose rest
    // global places it at +100 on X, with the layer parented to a layer at
    // +500 on X, and confirm the bone wins.
    Skeleton skeleton;
    Pose pose;
    build_two_bone_rig(skeleton, pose);

    Scene2D s;
    const LayerId parent = s.add_layer("parent");
    s.find(parent)->position = vec2(500.0f, 0.0f);
    const LayerId child = s.add_layer("arm", parent);
    s.find(child)->bound_bone = "arm";
    s.find(child)->position = vec2(0.0f, 0.0f);

    const vec2 p = s.world_pivot(child, &skeleton, &pose);
    // Bone "arm" sits at +100 on X; the layer's own position is zero, so the
    // pivot lands there and the +500 parent is ignored entirely.
    MS_CHECK(nearly(p, vec2(100.0f, 0.0f), 1e-3f));
}

void test_bone_binding_uses_posed_global() {
    // Moving the bone (via its pose delta) must move the bound layer.
    Skeleton skeleton;
    Pose pose;
    build_two_bone_rig(skeleton, pose);
    // Translate the child bone by +30 on Y in its local space.
    pose.delta[1].position = vec3(0.0f, 30.0f, 0.0f);
    evaluate_pose(skeleton, pose);

    Scene2D s;
    const LayerId id = s.add_layer("arm");
    s.find(id)->bound_bone = "arm";

    const vec2 p = s.world_pivot(id, &skeleton, &pose);
    MS_CHECK(nearly(p, vec2(100.0f, 30.0f), 1e-3f));
}

void test_bone_binding_unknown_bone_falls_back() {
    // An unknown bone name is not a binding: the layer keeps its normal parent
    // chain (here, the root, so its own position applies).
    Skeleton skeleton;
    Pose pose;
    build_two_bone_rig(skeleton, pose);

    Scene2D s;
    const LayerId id = s.add_layer("arm");
    s.find(id)->bound_bone = "does_not_exist";
    s.find(id)->position = vec2(7.0f, 9.0f);

    const vec2 p = s.world_pivot(id, &skeleton, &pose);
    MS_CHECK(nearly(p, vec2(7.0f, 9.0f), 1e-3f));
}

void test_bone_binding_through_parent_chain() {
    // A child of a bound layer inherits the bone transform through the normal
    // parent chain: the parent is placed by its bone, and the child offsets
    // from that.
    Skeleton skeleton;
    Pose pose;
    build_two_bone_rig(skeleton, pose);

    Scene2D s;
    const LayerId bound = s.add_layer("arm");
    s.find(bound)->bound_bone = "arm";
    const LayerId child = s.add_layer("hand", bound);
    s.find(child)->position = vec2(10.0f, 5.0f);

    const vec2 p = s.world_pivot(child, &skeleton, &pose);
    MS_CHECK(nearly(p, vec2(110.0f, 5.0f), 1e-3f));
}

void test_pivot_base_matrix_inverts_to_local() {
    // pivot_world_matrix() == pivot_base_matrix() * pivot_matrix(), which is
    // what the gizmo relies on to recover a local transform. Verify the
    // identity holds for a bound layer.
    Skeleton skeleton;
    Pose pose;
    build_two_bone_rig(skeleton, pose);

    Scene2D s;
    const LayerId id = s.add_layer("arm");
    Layer2D* layer = s.find(id);
    layer->bound_bone = "arm";
    layer->position = vec2(3.0f, 4.0f);
    layer->rotation = 30.0f;
    layer->pivot = vec2(2.0f, 1.0f);

    const mat4 base = s.pivot_base_matrix(id, &skeleton, &pose);
    const mat4 expected = base * layer->pivot_matrix();
    const mat4 actual = s.pivot_world_matrix(id, &skeleton, &pose);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            MS_CHECK(nearly(actual[c][r], expected[c][r], 1e-4f));
        }
    }
}

void test_project_round_trip_bone_binding() {
    // A layer's bound_bone and bind_offset survive a save/load cycle, and an
    // unbound layer writes neither key.
    Project p;
    p.editor_mode = "2d";
    ProjectLayer2D bound;
    bound.name = "arm";
    bound.image_path = "arm.png";
    bound.bound_bone = "shoulder";
    bound.bind_offset = vec2(4.0f, -2.0f);
    p.layers_2d.push_back(bound);
    ProjectLayer2D unbound;
    unbound.name = "bg";
    p.layers_2d.push_back(unbound);

    const std::string text = project_to_json(p);
    MS_CHECK(text.find("\"bound_bone\"") != std::string::npos);
    MS_CHECK(text.find("\"bind_offset\"") != std::string::npos);

    Project loaded;
    std::string error;
    MS_CHECK_MSG(project_from_json(text, loaded, error), error);
    MS_CHECK(loaded.layers_2d[0].bound_bone == "shoulder");
    MS_CHECK_NEAR(loaded.layers_2d[0].bind_offset, vec2(4.0f, -2.0f));
    MS_CHECK(loaded.layers_2d[1].bound_bone.empty());
    MS_CHECK_NEAR(loaded.layers_2d[1].bind_offset, vec2(0.0f, 0.0f));
}

void test_project_omits_empty_binding() {
    // An unbound layer (the common case, and every M6 file) writes no binding
    // keys at all, so old projects stay byte-identical.
    Project p;
    p.editor_mode = "2d";
    ProjectLayer2D layer;
    layer.name = "bg";
    layer.image_path = "bg.png";
    p.layers_2d.push_back(layer);

    const std::string text = project_to_json(p);
    MS_CHECK(text.find("\"bound_bone\"") == std::string::npos);
    MS_CHECK(text.find("\"bind_offset\"") == std::string::npos);
}

// --- M6B: onion skinning --------------------------------------------------

void test_onion_window_disabled_is_empty() {
    OnionSkinSettings s;
    s.enabled = false;
    s.prev_frames = 3;
    s.next_frames = 3;
    MS_CHECK(onion_skin_window(s, 10, 0, 100).empty());
}

void test_onion_window_orders_far_to_near() {
    // With 2 previous and 1 next frame at frame 10, the window is
    // {8, 9, 11}, past frames first (farthest first) then future.
    OnionSkinSettings s;
    s.enabled = true;
    s.prev_frames = 2;
    s.next_frames = 1;
    const std::vector<OnionFrame> w = onion_skin_window(s, 10, 0, 100);
    MS_CHECK(w.size() == 3);
    MS_CHECK(w[0].frame == 8 && w[0].offset == -2);
    MS_CHECK(w[1].frame == 9 && w[1].offset == -1);
    MS_CHECK(w[2].frame == 11 && w[2].offset == 1);
}

void test_onion_window_clamps_to_timeline() {
    // Frames outside [start, end] are dropped, and the current frame is never
    // included. At frame 1 of [1, 5] with 2 prev / 2 next, only {2, 3} remain.
    OnionSkinSettings s;
    s.enabled = true;
    s.prev_frames = 2;
    s.next_frames = 2;
    const std::vector<OnionFrame> w = onion_skin_window(s, 1, 1, 5);
    MS_CHECK(w.size() == 2);
    MS_CHECK(w[0].frame == 3 && w[0].offset == 2);
    MS_CHECK(w[1].frame == 2 && w[1].offset == 1);
    for (const OnionFrame& f : w) {
        MS_CHECK(f.frame != 1);
    }
}

void test_onion_ghost_tint_direction_and_fade() {
    // Past frames are cool (blue dominates), future frames warm (red
    // dominates), and a farther ghost is fainter than a nearer one.
    const vec4 past_near = onion_ghost_tint(-1, 0.5f);
    const vec4 past_far = onion_ghost_tint(-3, 0.5f);
    const vec4 future = onion_ghost_tint(1, 0.5f);
    MS_CHECK(past_near.b > past_near.r);
    MS_CHECK(future.r > future.b);
    MS_CHECK(past_near.a > past_far.a);
    MS_CHECK_NEAR(past_near.a, 0.5f);
}

}  // namespace

int main() {
    test_add_and_find();
    test_parenting_to_missing_is_root();
    test_local_matrix_translates();
    test_pivot_shifts_rotation_origin();
    test_world_matrix_composes_parent();
    test_parent_rotation_rotates_child();
    test_world_pivot();
    test_draw_order_sorts_by_z_stable();
    test_selection();

    test_vec2_track_linear_interpolation();
    test_vec2_track_holds_outside_range();
    test_vec2_track_set_key_replaces();
    test_vec2_track_keeps_sorted_on_out_of_order_insert();
    test_vec2_track_bezier_easing();
    test_float_track_linear();
    test_layer_2d_track_samples_only_keyed_channels();
    test_layer_2d_track_empty();
    test_apply_layer_2d_clip();
    test_clip_frame_end_includes_2d();

    test_project_round_trip_2d();
    test_project_without_2d_defaults_to_3d();
    test_project_2d_json_key_names();
    test_project_rejects_bad_editor_mode();

    test_projection_2d_maps_pixels_to_clip();

    test_bone_binding_overrides_parent();
    test_bone_binding_uses_posed_global();
    test_bone_binding_unknown_bone_falls_back();
    test_bone_binding_through_parent_chain();
    test_pivot_base_matrix_inverts_to_local();
    test_project_round_trip_bone_binding();
    test_project_omits_empty_binding();
    test_onion_window_disabled_is_empty();
    test_onion_window_orders_far_to_near();
    test_onion_window_clamps_to_timeline();
    test_onion_ghost_tint_direction_and_fade();
    return summary("test_2d");
}
