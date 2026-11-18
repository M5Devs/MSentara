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

// Builds a synthetic VMD in memory and checks the parser, the Shift-JIS
// decoder, the Bezier interpolation and the coordinate conversion end to end.
// No copyrighted motion is involved: the bytes are generated here, which also
// lets the test pin down exact field offsets.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "TestCommon.hpp"
#include "core/Animation.hpp"
#include "core/Skeleton.hpp"
#include "formats/ShiftJis.hpp"
#include "formats/VmdLoader.hpp"

namespace {

using namespace ms;
using namespace ms::test;

// ---------------------------------------------------------------------------
// VMD writer
// ---------------------------------------------------------------------------

class VmdBuilder {
public:
    void u8(std::uint8_t v) { bytes_.push_back(v); }
    void u32(std::uint32_t v) {
        bytes_.push_back(static_cast<std::uint8_t>(v & 0xFFu));
        bytes_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
        bytes_.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
        bytes_.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    }
    void f32(float v) {
        std::uint32_t raw = 0;
        std::memcpy(&raw, &v, sizeof(raw));
        u32(raw);
    }
    void vec3(float x, float y, float z) {
        f32(x);
        f32(y);
        f32(z);
    }

    // Writes a fixed NUL-padded field, truncating anything past `size`.
    void fixed(const std::vector<std::uint8_t>& data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            bytes_.push_back(i < data.size() ? data[i] : 0);
        }
    }

    // The 64-byte interpolation block: four copies of a 16-byte pattern where
    // byte (control * 4 + channel) holds that control value.
    void interpolation(std::uint8_t x0, std::uint8_t y0, std::uint8_t x1,
                       std::uint8_t y1) {
        for (int copy = 0; copy < 4; ++copy) {
            for (int control = 0; control < 4; ++control) {
                const std::uint8_t value =
                    (control == 0) ? x0 : (control == 1) ? y0
                                        : (control == 2) ? x1
                                                         : y1;
                for (int channel = 0; channel < 4; ++channel) {
                    bytes_.push_back(value);
                }
            }
        }
    }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

// Shift-JIS bytes for the Japanese bone names used below, so the test does not
// depend on a compiler source-encoding assumption.
const std::vector<std::uint8_t> kCenterSjis = {0x83, 0x5A, 0x83, 0x93,
                                               0x83, 0x5E, 0x81, 0x5B};  // センター
const std::vector<std::uint8_t> kLeftArmSjis = {0x8D, 0xB6, 0x98,
                                                0x72};  // 左腕

void write_header(VmdBuilder& b, const std::string& signature,
                  std::size_t model_name_size, const std::string& model_name) {
    std::vector<std::uint8_t> sig(signature.begin(), signature.end());
    b.fixed(sig, 30);
    std::vector<std::uint8_t> name(model_name.begin(), model_name.end());
    b.fixed(name, model_name_size);
}

void write_bone_key(VmdBuilder& b, const std::vector<std::uint8_t>& name,
                    std::uint32_t frame, float tx, float ty, float tz,
                    float qx, float qy, float qz, float qw,
                    std::uint8_t x0 = 0, std::uint8_t y0 = 0,
                    std::uint8_t x1 = 127, std::uint8_t y1 = 127) {
    b.fixed(name, 15);
    b.u32(frame);
    b.vec3(tx, ty, tz);
    b.f32(qx);
    b.f32(qy);
    b.f32(qz);
    b.f32(qw);
    b.interpolation(x0, y0, x1, y1);
}

// The camera key's 24-byte interpolation block: six sequential 4-byte curves
// (X, Y, Z, rotation, distance, FOV), each stored as (x0, x1, y0, y1). This is
// deliberately a different layout from the bone block's interleaved bytes, so
// a regression that reuses the bone reader would fail these tests.
void write_camera_key(VmdBuilder& b, std::uint32_t frame, float distance,
                      float ix, float iy, float iz, float rx, float ry,
                      float rz, std::uint32_t angle_deg,
                      std::uint8_t perspective,
                      const std::uint8_t interp[24] = nullptr) {
    b.u32(frame);
    b.f32(distance);
    b.vec3(ix, iy, iz);
    b.vec3(rx, ry, rz);
    if (interp != nullptr) {
        for (int i = 0; i < 24; ++i) {
            b.u8(interp[i]);
        }
    } else {
        for (int i = 0; i < 24; ++i) {
            // 20/107/20/107 is the neutral (linear-ish) MMD default.
            b.u8((i % 4 == 0 || i % 4 == 2) ? 20 : 107);
        }
    }
    b.u32(angle_deg);
    b.u8(perspective);
}

// ---------------------------------------------------------------------------
// Shift-JIS decoding
// ---------------------------------------------------------------------------

void test_sjis_decodes_katakana() {
    // センター in Shift-JIS is the UTF-8 sequence E3 82 BB E3 83 B3 E3 82 BF
    // E3 83 BC.
    const std::string utf8 = shift_jis_to_utf8(kCenterSjis.data(),
                                               kCenterSjis.size());
    MS_CHECK(utf8 == "\xE3\x82\xBB\xE3\x83\xB3\xE3\x82\xBF\xE3\x83\xBC");
}

void test_sjis_decodes_kanji() {
    // 左腕 -> E5 B7 A6 E8 85 95
    const std::string utf8 = shift_jis_to_utf8(kLeftArmSjis.data(),
                                               kLeftArmSjis.size());
    MS_CHECK(utf8 == "\xE5\xB7\xA6\xE8\x85\x95");
}

void test_sjis_ascii_passthrough() {
    const std::string ascii = "center";
    const std::string utf8 = shift_jis_to_utf8(ascii.data(), ascii.size());
    MS_CHECK(utf8 == "center");
}

void test_sjis_stops_at_nul() {
    // A fixed field is NUL-padded; decoding must stop at the NUL, not read the
    // padding as more characters.
    const std::uint8_t field[15] = {'a', 'b', 0x00, 'c', 0x00};
    const std::string utf8 = shift_jis_to_utf8(field, sizeof(field));
    MS_CHECK(utf8 == "ab");
}

void test_sjis_empty_field() {
    const std::uint8_t field[15] = {0};
    MS_CHECK(shift_jis_to_utf8(field, sizeof(field)).empty());
}

// ---------------------------------------------------------------------------
// Bezier interpolation
// ---------------------------------------------------------------------------

void test_bezier_linear_is_identity() {
    const Bezier b = Bezier::linear();
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        MS_CHECK(nearly(b.ease(t), t, 1e-3f));
    }
}

void test_bezier_endpoints() {
    Bezier b;
    b.cp1 = vec2(0.0f, 1.0f);
    b.cp2 = vec2(1.0f, 0.0f);
    MS_CHECK(nearly(b.ease(0.0f), 0.0f));
    MS_CHECK(nearly(b.ease(1.0f), 1.0f));
}

void test_bezier_ease_in_out_matches_reference() {
    // Control points (0, 127) and (127, 0) — the strongest ease-in/out curve.
    // These eased fractions are the reference values the bisection must
    // reproduce (computed from the same cubic the reference implementation
    // evaluates).
    Bezier b;
    b.cp1 = vec2(0.0f, 1.0f);
    b.cp2 = vec2(1.0f, 0.0f);
    MS_CHECK(nearly(b.ease(0.25f), 0.479057f, 1e-4f));
    MS_CHECK(nearly(b.ease(0.5f), 0.5f, 1e-4f));
    MS_CHECK(nearly(b.ease(0.75f), 0.520943f, 1e-4f));
}

void test_bezier_is_monotonic() {
    Bezier b;
    b.cp1 = vec2(0.2f, 0.9f);
    b.cp2 = vec2(0.8f, 0.1f);
    float previous = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float v = b.ease(static_cast<float>(i) / 20.0f);
        MS_CHECK(v >= previous - 1e-4f);
        previous = v;
    }
}

// ---------------------------------------------------------------------------
// Header parsing
// ---------------------------------------------------------------------------

void test_header_version_two_and_model_name() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "Test Model");
    b.u32(0);  // no bone keyframes
    b.u32(0);  // no morph keyframes

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    MS_CHECK(r.model_name == "Test Model");
    MS_CHECK(r.clip.bone_tracks.empty());
}

void test_header_old_variant_uses_ten_byte_name() {
    // The pre-0002 header has a 10-byte model name field.
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data file", 10, "Old");
    b.u32(0);
    b.u32(0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    MS_CHECK(r.model_name == "Old");
}

void test_bad_header_is_rejected() {
    VmdBuilder b;
    write_header(b, "Not A VMD File At All", 20, "x");
    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(!r.ok);
    MS_CHECK(!r.error.empty());
}

void test_truncated_buffer_is_rejected() {
    const std::uint8_t tiny[4] = {0x50, 0x4D, 0x58, 0x20};
    const VmdResult r = parse_vmd(tiny, sizeof(tiny));
    MS_CHECK(!r.ok);
}

// ---------------------------------------------------------------------------
// Bone keyframes
// ---------------------------------------------------------------------------

void test_bone_keyframe_parsed_and_named() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(1);
    write_bone_key(b, kCenterSjis, 12, 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f,
                   1.0f);
    b.u32(0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    MS_CHECK(r.clip.bone_tracks.size() == 1);
    const BoneTrack& t = r.clip.bone_tracks[0];
    MS_CHECK(t.name == "\xE3\x82\xBB\xE3\x83\xB3\xE3\x82\xBF\xE3\x83\xBC");
    MS_CHECK(t.keys.size() == 1);
    MS_CHECK(t.keys[0].frame == 12);
}

void test_keys_are_sorted_by_frame() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(3);
    write_bone_key(b, kCenterSjis, 30, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    write_bone_key(b, kCenterSjis, 5, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    write_bone_key(b, kCenterSjis, 17, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    b.u32(0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    MS_CHECK(r.clip.bone_tracks.size() == 1);
    const auto& keys = r.clip.bone_tracks[0].keys;
    MS_CHECK(keys.size() == 3);
    MS_CHECK(keys[0].frame == 5);
    MS_CHECK(keys[1].frame == 17);
    MS_CHECK(keys[2].frame == 30);
}

void test_coordinate_conversion_translation() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(1);
    // MMD translation (1, 2, 3) mirrors to (1, 2, -3).
    write_bone_key(b, kCenterSjis, 0, 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    b.u32(0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    MS_CHECK_NEAR(r.clip.bone_tracks[0].keys[0].translation,
                  vec3(1.0f, 2.0f, -3.0f));
}

void test_coordinate_conversion_rotation_x() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(1);
    // +90 degrees about X: q = (sin45, 0, 0, cos45). The Z mirror turns this
    // into -90 degrees about X: q = (-sin45, 0, 0, cos45).
    const float s = 0.70710678f;
    write_bone_key(b, kCenterSjis, 0, 0.0f, 0.0f, 0.0f, s, 0.0f, 0.0f, s);
    b.u32(0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    const quat q = r.clip.bone_tracks[0].keys[0].rotation;
    MS_CHECK(nearly(q.x, -s, 1e-3f));
    MS_CHECK(nearly(q.y, 0.0f, 1e-3f));
    MS_CHECK(nearly(q.z, 0.0f, 1e-3f));
    MS_CHECK(nearly(q.w, s, 1e-3f));
}

void test_coordinate_conversion_rotation_z_unchanged() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(1);
    // A rotation about Z is untouched by a Z mirror.
    const float s = 0.70710678f;
    write_bone_key(b, kCenterSjis, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, s, s);
    b.u32(0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    const quat q = r.clip.bone_tracks[0].keys[0].rotation;
    MS_CHECK(nearly(q.z, s, 1e-3f));
    MS_CHECK(nearly(q.w, s, 1e-3f));
}

// ---------------------------------------------------------------------------
// Morph keyframes
// ---------------------------------------------------------------------------

void test_morph_keyframes_become_scalar_tracks() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(0);  // no bone keyframes
    b.u32(2);  // two morph keyframes
    const std::vector<std::uint8_t> smile = {'s', 'm', 'i', 'l', 'e'};
    b.fixed(smile, 15);
    b.u32(0);
    b.f32(0.0f);
    b.fixed(smile, 15);
    b.u32(10);
    b.f32(1.0f);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    MS_CHECK(r.clip.scalar_tracks.size() == 1);
    const ScalarTrack& t = r.clip.scalar_tracks[0];
    MS_CHECK(t.name == "smile");
    MS_CHECK(t.keys.size() == 2);
    MS_CHECK(nearly(t.sample(5.0f), 0.5f));
}

// ---------------------------------------------------------------------------
// Camera keyframes
// ---------------------------------------------------------------------------

void test_camera_section_parsed() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(0);  // no bone keyframes
    b.u32(0);  // no morph keyframes
    b.u32(2);  // two camera keyframes
    // frame 0: interest (1, 2, 3), distance 40, fov 30, perspective.
    write_camera_key(b, 0, 40.0f, 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 30, 0);
    // frame 30: interest (0, 10, 0), distance 55, fov 45, orthographic.
    write_camera_key(b, 30, 55.0f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 45, 1);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    MS_CHECK(r.clip.camera_track.keys.size() == 2);

    const CameraKey& k0 = r.clip.camera_track.keys[0];
    MS_CHECK(k0.frame == 0);
    MS_CHECK(nearly(k0.state.distance, 40.0f, 1e-3f));
    // The interest point is mirrored on Z (MMD left-handed -> right-handed).
    MS_CHECK_NEAR(k0.state.interest, vec3(1.0f, 2.0f, -3.0f));
    MS_CHECK(nearly(k0.state.fov, 30.0f, 1e-3f));
    MS_CHECK(k0.state.perspective);

    const CameraKey& k1 = r.clip.camera_track.keys[1];
    MS_CHECK(k1.frame == 30);
    MS_CHECK(nearly(k1.state.distance, 55.0f, 1e-3f));
    MS_CHECK(nearly(k1.state.fov, 45.0f, 1e-3f));
    MS_CHECK(!k1.state.perspective);
}

void test_camera_interpolation_is_sequential_layout() {
    // The X curve is the first 4 bytes, the Y curve the next 4, and so on.
    // Setting them to different patterns proves the reader does not reuse the
    // bone block's interleaved (control * 4 + channel) layout.
    std::uint8_t interp[24] = {};
    // X: (x0=0, x1=127, y0=0, y1=127) -> linear.
    interp[0] = 0;   interp[1] = 127; interp[2] = 0;   interp[3] = 127;
    // Y: (x0=127, x1=0, y0=127, y1=0) -> reversed control points.
    interp[4] = 127; interp[5] = 0;   interp[6] = 127; interp[7] = 0;

    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(0);
    b.u32(0);
    b.u32(1);
    write_camera_key(b, 0, 40.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 30, 0,
                     interp);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    MS_CHECK(r.clip.camera_track.keys.size() == 1);
    const CameraKey& k = r.clip.camera_track.keys[0];

    // The X curve came out linear: (0,0) and (1,1).
    MS_CHECK_NEAR(k.bezier_x.cp1, vec2(0.0f, 0.0f));
    MS_CHECK_NEAR(k.bezier_x.cp2, vec2(1.0f, 1.0f));
    MS_CHECK(nearly(k.bezier_x.ease(0.5f), 0.5f, 1e-3f));
    // The Y curve got the reversed control points: (1,1) and (0,0).
    MS_CHECK_NEAR(k.bezier_y.cp1, vec2(1.0f, 1.0f));
    MS_CHECK_NEAR(k.bezier_y.cp2, vec2(0.0f, 0.0f));
}

void test_camera_frame_end_includes_camera_keys() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(0);
    b.u32(0);
    b.u32(1);
    write_camera_key(b, 77, 40.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 30, 0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    MS_CHECK(r.clip.has_camera());
    MS_CHECK(r.clip.frame_end() == 77);
}

void test_camera_track_sampling_midpoint() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(0);
    b.u32(0);
    b.u32(2);
    write_camera_key(b, 0, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 20, 0);
    write_camera_key(b, 10, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 40, 0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    // The default interpolation is a symmetric ease-in-out, so the midpoint is
    // still the average of the two keys.
    const CameraState mid = r.clip.camera_track.sample(5.0f);
    MS_CHECK(nearly(mid.distance, 20.0f, 1e-2f));
    MS_CHECK(nearly(mid.fov, 30.0f, 1e-2f));

    // Outside the range the nearest key is held.
    MS_CHECK(nearly(r.clip.camera_track.sample(-5.0f).distance, 10.0f, 1e-3f));
    MS_CHECK(nearly(r.clip.camera_track.sample(100.0f).distance, 30.0f, 1e-3f));
}

void test_camera_track_hard_cut_on_adjacent_keys() {
    // Two keys one frame apart are a camera cut: the earlier key is held across
    // the span rather than interpolated (this is how MMD marks a switch).
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(0);
    b.u32(0);
    b.u32(2);
    write_camera_key(b, 0, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 20, 0);
    write_camera_key(b, 1, 90.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 20, 0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    const CameraState cut = r.clip.camera_track.sample(0.5f);
    MS_CHECK(nearly(cut.distance, 10.0f, 1e-3f));
}

void test_apply_camera_clip_drives_camera() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(0);
    b.u32(0);
    b.u32(1);
    // Rotation is stored in radians in CameraState; use 0 here and pin the
    // fields that do not depend on the Euler convention.
    write_camera_key(b, 0, 42.0f, 3.0f, 4.0f, 5.0f, 0.0f, 0.0f, 0.0f, 25, 0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);

    Camera cam;
    MS_CHECK(apply_camera_clip(r.clip, 0.0f, cam));
    MS_CHECK_NEAR(cam.target, vec3(3.0f, 4.0f, -5.0f));
    MS_CHECK(nearly(cam.distance, 42.0f, 1e-3f));
    MS_CHECK(nearly(cam.fov_y, 25.0f, 1e-3f));
    MS_CHECK(!cam.orthographic);
}

void test_apply_camera_clip_noop_without_camera() {
    AnimationClip clip;
    clip.bone("center").keys.push_back(BoneKey{});
    Camera cam;
    const float yaw_before = cam.yaw;
    MS_CHECK(!apply_camera_clip(clip, 0.0f, cam));
    MS_CHECK(nearly(cam.yaw, yaw_before, 1e-3f));
}

// ---------------------------------------------------------------------------
// Sampling and clip application
// ---------------------------------------------------------------------------

void test_track_sampling_interpolates_translation() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(2);
    write_bone_key(b, kCenterSjis, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    write_bone_key(b, kCenterSjis, 10, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    b.u32(0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.ok);
    vec3 t;
    quat q;
    r.clip.bone_tracks[0].sample(5.0f, t, q);
    // Linear interpolation between (0,0,0) and (10,0,0) at the midpoint.
    MS_CHECK(nearly(t.x, 5.0f, 1e-3f));
    MS_CHECK(nearly(t.y, 0.0f, 1e-3f));
    MS_CHECK(nearly(t.z, 0.0f, 1e-3f));
}

void test_track_sampling_holds_outside_range() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(2);
    write_bone_key(b, kCenterSjis, 10, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    write_bone_key(b, kCenterSjis, 20, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    b.u32(0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    vec3 t;
    quat q;
    r.clip.bone_tracks[0].sample(-5.0f, t, q);
    MS_CHECK(nearly(t.x, 1.0f, 1e-3f));
    r.clip.bone_tracks[0].sample(100.0f, t, q);
    MS_CHECK(nearly(t.x, 2.0f, 1e-3f));
}

void test_clip_frame_end() {
    VmdBuilder b;
    write_header(b, "Vocaloid Motion Data 0002", 20, "M");
    b.u32(2);
    write_bone_key(b, kCenterSjis, 4, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    write_bone_key(b, kCenterSjis, 42, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    b.u32(0);

    const VmdResult r = parse_vmd(b.bytes().data(), b.bytes().size());
    MS_CHECK(r.clip.frame_end() == 42);
}

void test_apply_clip_matches_bone_by_name() {
    // Rig: root "center" with child "left arm".
    Skeleton skeleton;
    skeleton.add_bone("center", -1, vec3(0.0f, 0.0f, 0.0f));
    skeleton.add_bone("left arm", 0, vec3(1.0f, 0.0f, 0.0f));
    skeleton.compute_rest();

    AnimationClip clip;
    BoneTrack& track = clip.bone("left arm");
    BoneKey key;
    key.frame = 0;
    key.translation = vec3(0.0f, 5.0f, 0.0f);
    track.keys.push_back(key);

    Pose pose;
    pose.resize(skeleton.size());
    apply_clip(skeleton, clip, 0.0f, pose);

    // The matched bone got the sampled delta; the unmatched root stayed at rest.
    MS_CHECK_NEAR(pose.delta[1].position, vec3(0.0f, 5.0f, 0.0f));
    MS_CHECK_NEAR(pose.delta[0].position, vec3(0.0f, 0.0f, 0.0f));

    // With the delta applied, the child's model-space origin moved by 5 on Y.
    evaluate_pose(skeleton, pose);
    MS_CHECK_NEAR(vec3(pose.global[1][3]), vec3(1.0f, 5.0f, 0.0f));
}

void test_apply_clip_ignores_unknown_bones() {
    Skeleton skeleton;
    skeleton.add_bone("center", -1, vec3(0.0f, 0.0f, 0.0f));
    skeleton.compute_rest();

    AnimationClip clip;
    clip.bone("does not exist").keys.push_back(BoneKey{});

    Pose pose;
    pose.resize(skeleton.size());
    apply_clip(skeleton, clip, 0.0f, pose);
    // Nothing matched, so the pose is untouched.
    MS_CHECK_NEAR(pose.delta[0].position, vec3(0.0f, 0.0f, 0.0f));
}

void test_apply_clip_rotation_matches_expected_matrix() {
    Skeleton skeleton;
    skeleton.add_bone("center", -1, vec3(0.0f, 0.0f, 0.0f));
    skeleton.compute_rest();

    AnimationClip clip;
    BoneTrack& track = clip.bone("center");
    BoneKey key;
    key.frame = 0;
    // 90 degrees about Y as a quaternion.
    const float s = 0.70710678f;
    key.rotation = quat(s, 0.0f, s, 0.0f);
    track.keys.push_back(key);

    Pose pose;
    pose.resize(skeleton.size());
    apply_clip(skeleton, clip, 0.0f, pose);

    // The delta must reproduce that rotation: applying it to +X gives -Z
    // (right-handed 90 degrees about Y).
    const vec3 rotated = mat3(pose.delta[0].matrix()) * vec3(1.0f, 0.0f, 0.0f);
    MS_CHECK(nearly(rotated.x, 0.0f, 1e-3f));
    MS_CHECK(nearly(rotated.z, -1.0f, 1e-3f));
}

}  // namespace

int main() {
    test_sjis_decodes_katakana();
    test_sjis_decodes_kanji();
    test_sjis_ascii_passthrough();
    test_sjis_stops_at_nul();
    test_sjis_empty_field();

    test_bezier_linear_is_identity();
    test_bezier_endpoints();
    test_bezier_ease_in_out_matches_reference();
    test_bezier_is_monotonic();

    test_header_version_two_and_model_name();
    test_header_old_variant_uses_ten_byte_name();
    test_bad_header_is_rejected();
    test_truncated_buffer_is_rejected();

    test_bone_keyframe_parsed_and_named();
    test_keys_are_sorted_by_frame();
    test_coordinate_conversion_translation();
    test_coordinate_conversion_rotation_x();
    test_coordinate_conversion_rotation_z_unchanged();

    test_morph_keyframes_become_scalar_tracks();

    test_camera_section_parsed();
    test_camera_interpolation_is_sequential_layout();
    test_camera_frame_end_includes_camera_keys();
    test_camera_track_sampling_midpoint();
    test_camera_track_hard_cut_on_adjacent_keys();
    test_apply_camera_clip_drives_camera();
    test_apply_camera_clip_noop_without_camera();

    test_track_sampling_interpolates_translation();
    test_track_sampling_holds_outside_range();
    test_clip_frame_end();
    test_apply_clip_matches_bone_by_name();
    test_apply_clip_ignores_unknown_bones();
    test_apply_clip_rotation_matches_expected_matrix();

    return ms::test::summary("vmd");
}
