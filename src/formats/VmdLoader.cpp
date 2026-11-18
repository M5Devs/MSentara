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

#include "formats/VmdLoader.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/Animation.hpp"
#include "core/Math.hpp"
#include "formats/ShiftJis.hpp"

namespace ms {

namespace {

constexpr std::size_t kHeaderSize = 30;
constexpr std::size_t kBoneNameSize = 15;
constexpr std::size_t kMorphNameSize = 15;
constexpr std::size_t kInterpolationSize = 64;

// Bounds-checked little-endian reader, mirroring the PMX loader's Reader so
// the two parsers behave identically on truncated input.
class VmdReader {
public:
    VmdReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    bool ok() const { return ok_; }
    std::size_t remaining() const { return size_ - offset_; }
    const std::string& error() const { return error_; }

    bool bytes(std::uint8_t* out, std::size_t count) {
        if (!ok_ || offset_ + count > size_) {
            return fail("unexpected end of VMD data");
        }
        std::memcpy(out, data_ + offset_, count);
        offset_ += count;
        return true;
    }

    bool u32(std::uint32_t& out) {
        if (!ok_ || offset_ + 4 > size_) {
            return fail("unexpected end of VMD data");
        }
        out = static_cast<std::uint32_t>(data_[offset_]) |
              (static_cast<std::uint32_t>(data_[offset_ + 1]) << 8) |
              (static_cast<std::uint32_t>(data_[offset_ + 2]) << 16) |
              (static_cast<std::uint32_t>(data_[offset_ + 3]) << 24);
        offset_ += 4;
        return true;
    }

    bool f32(float& out) {
        std::uint32_t raw = 0;
        if (!u32(raw)) {
            return false;
        }
        std::memcpy(&out, &raw, sizeof(out));
        return true;
    }

    bool vec3f(vec3& out) {
        return f32(out.x) && f32(out.y) && f32(out.z);
    }

    bool fail(const std::string& message) {
        if (ok_) {
            ok_ = false;
            error_ = message;
        }
        return false;
    }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
    bool ok_ = true;
    std::string error_;
};

// VMD interpolation bytes are in [0, 127]; the Bezier control points are those
// values normalised by 127, matching saba's SetVMDBezier exactly.
Bezier make_bezier(std::uint8_t x0, std::uint8_t y0, std::uint8_t x1,
                   std::uint8_t y1) {
    Bezier b;
    b.cp1 = vec2(static_cast<float>(x0) / 127.0f,
                 static_cast<float>(y0) / 127.0f);
    b.cp2 = vec2(static_cast<float>(x1) / 127.0f,
                 static_cast<float>(y1) / 127.0f);
    return b;
}

// The 64-byte interpolation block is four identical 16-byte copies of the
// same 16 values. Each 16-byte copy holds four control values (x0, y0, x1,
// y1) for each of the four channels (X, Y, Z, rotation), interleaved as
// [x0_X, x0_Y, x0_Z, x0_R, y0_X, y0_Y, y0_Z, y0_R, x1_..., y1_...]. That is
// byte (control * 4 + channel), which is exactly how saba reads it: the
// Bezier for channel c is built from interpolation[c + 0], [c + 4],
// [c + 8] and [c + 12].
void read_interpolation(const std::uint8_t* interp, BoneKey& key) {
    const auto at = [&](int control, int channel) {
        return interp[control * 4 + channel];
    };
    key.bezier_x = make_bezier(at(0, 0), at(1, 0), at(2, 0), at(3, 0));
    key.bezier_y = make_bezier(at(0, 1), at(1, 1), at(2, 1), at(3, 1));
    key.bezier_z = make_bezier(at(0, 2), at(1, 2), at(2, 2), at(3, 2));
    key.bezier_rot = make_bezier(at(0, 3), at(1, 3), at(2, 3), at(3, 3));
}

// MMD is left-handed Y-up Z-forward; PMX loading mirrors Z to reach the
// right-handed space. The same mirror applied to a rotation quaternion
// (x, y, z, w) is (-x, -y, z, w): conjugating the rotation matrix by
// diag(1, 1, -1), which keeps it a proper rotation.
vec3 mirror_translation(const vec3& t) {
    return vec3(t.x, t.y, -t.z);
}

quat mirror_rotation(const quat& q) {
    return glm::normalize(quat(q.w, -q.x, -q.y, q.z));
}

// A VMD camera key's 24-byte interpolation block holds *six* sequential 4-byte
// curves, in the order X, Y, Z, rotation, distance, FOV. Each 4-byte curve is
// (x0, x1, y0, y1) — the reference implementation (saba's
// VMDCameraAnimation::Create) reads ip[0..3] for X, ip[4..7] for Y, and so on,
// and passes them to SetVMDBezier as (x0, x1, y0, y1). This is a different
// layout from the bone block's interleaved (control * 4 + channel) bytes.
void read_camera_interpolation(const std::uint8_t* ip, CameraKey& key) {
    key.bezier_x = make_bezier(ip[0], ip[2], ip[1], ip[3]);
    key.bezier_y = make_bezier(ip[4], ip[6], ip[5], ip[7]);
    key.bezier_z = make_bezier(ip[8], ip[10], ip[9], ip[11]);
    key.bezier_rot = make_bezier(ip[12], ip[14], ip[13], ip[15]);
    key.bezier_distance = make_bezier(ip[16], ip[18], ip[17], ip[19]);
    key.bezier_fov = make_bezier(ip[20], ip[22], ip[21], ip[23]);
}

}  // namespace

VmdResult parse_vmd(const std::uint8_t* data, std::size_t size) {
    VmdResult result;
    if (data == nullptr || size < kHeaderSize) {
        result.error = "empty or truncated VMD data";
        return result;
    }

    VmdReader r(data, size);

    std::uint8_t header[kHeaderSize] = {};
    if (!r.bytes(header, kHeaderSize)) {
        result.error = r.error();
        return result;
    }

    // "Vocaloid Motion Data 0002" is the current header; the older
    // "Vocaloid Motion Data" (with a "file" suffix) uses a 10-byte model name.
    const std::string header_text(reinterpret_cast<const char*>(header),
                                  kHeaderSize);
    if (header_text.rfind("Vocaloid Motion Data", 0) != 0) {
        result.error = "not a VMD file (bad header)";
        return result;
    }
    const bool version_two = header_text.find("0002") != std::string::npos;
    const std::size_t model_name_size = version_two ? 20u : 10u;

    std::uint8_t model_name[20] = {};
    if (!r.bytes(model_name, model_name_size)) {
        result.error = r.error();
        return result;
    }
    result.model_name =
        shift_jis_to_utf8(model_name, model_name_size);

    // --- Bone keyframes ---
    std::uint32_t motion_count = 0;
    if (!r.u32(motion_count)) {
        result.error = r.error();
        return result;
    }
    // One bone keyframe is 15 + 4 + 12 + 16 + 64 = 111 bytes; guard against a
    // bogus count claiming more than the file can hold.
    if (motion_count > (r.remaining() / 111u) + 1u) {
        result.error = "VMD bone keyframe count exceeds file size";
        return result;
    }

    for (std::uint32_t i = 0; i < motion_count; ++i) {
        std::uint8_t name[kBoneNameSize] = {};
        std::uint32_t frame = 0;
        vec3 translation;
        float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
        std::uint8_t interp[kInterpolationSize] = {};

        if (!r.bytes(name, kBoneNameSize) || !r.u32(frame) ||
            !r.vec3f(translation) || !r.f32(qx) || !r.f32(qy) ||
            !r.f32(qz) || !r.f32(qw) || !r.bytes(interp, kInterpolationSize)) {
            result.error = r.error();
            return result;
        }

        BoneKey key;
        key.frame = static_cast<int>(frame);
        key.translation = mirror_translation(translation);
        key.rotation = mirror_rotation(quat(qw, qx, qy, qz));
        read_interpolation(interp, key);

        const std::string bone_name = shift_jis_to_utf8(name, kBoneNameSize);
        if (!bone_name.empty()) {
            result.clip.bone(bone_name).keys.push_back(key);
        }
    }

    // --- Morph keyframes (optional; older files stop after the motions) ---
    if (r.remaining() >= 4u) {
        std::uint32_t morph_count = 0;
        if (!r.u32(morph_count)) {
            result.error = r.error();
            return result;
        }
        if (morph_count > (r.remaining() / 23u) + 1u) {
            result.error = "VMD morph keyframe count exceeds file size";
            return result;
        }
        for (std::uint32_t i = 0; i < morph_count; ++i) {
            std::uint8_t name[kMorphNameSize] = {};
            std::uint32_t frame = 0;
            float weight = 0.0f;
            if (!r.bytes(name, kMorphNameSize) || !r.u32(frame) ||
                !r.f32(weight)) {
                result.error = r.error();
                return result;
            }
            const std::string morph_name =
                shift_jis_to_utf8(name, kMorphNameSize);
            if (!morph_name.empty()) {
                ScalarTrack& track = result.clip.scalar(morph_name);
                track.keys.push_back(ScalarKey{static_cast<int>(frame),
                                               weight});
            }
        }
    }

    // --- Camera keyframes (optional) ---
    // One camera keyframe is 4 + 4 + 12 + 12 + 24 + 4 + 1 = 61 bytes. The
    // section is optional: a file may stop after the morphs.
    if (r.remaining() >= 4u) {
        std::uint32_t camera_count = 0;
        if (!r.u32(camera_count)) {
            result.error = r.error();
            return result;
        }
        if (camera_count > (r.remaining() / 61u) + 1u) {
            result.error = "VMD camera keyframe count exceeds file size";
            return result;
        }
        for (std::uint32_t i = 0; i < camera_count; ++i) {
            std::uint32_t frame = 0;
            float distance = 0.0f;
            vec3 interest;
            vec3 rotate;
            std::uint8_t interp[24] = {};
            std::uint32_t view_angle = 0;
            std::uint8_t perspective = 0;
            if (!r.u32(frame) || !r.f32(distance) || !r.vec3f(interest) ||
                !r.vec3f(rotate) || !r.bytes(interp, sizeof(interp)) ||
                !r.u32(view_angle) || !r.bytes(&perspective, 1)) {
                result.error = r.error();
                return result;
            }

            CameraKey key;
            key.frame = static_cast<int>(frame);
            // The interest point mirrors like any other position. The
            // orientation is built by Camera::apply_vmd_state from the raw
            // Euler angles, which it composes as Ry * Rz * Rx — the exact
            // form the reference implementation (saba's MMDLookAtCamera)
            // uses — so the angles are stored unmirrored.
            key.state.interest = mirror_translation(interest);
            key.state.rotate = rotate;
            key.state.distance = distance;
            key.state.fov = static_cast<float>(view_angle);
            key.state.perspective = (perspective == 0);
            read_camera_interpolation(interp, key);
            result.clip.camera_track.keys.push_back(key);
        }
        result.clip.camera_track.sort_keys();
    }

    for (BoneTrack& track : result.clip.bone_tracks) {
        track.sort_keys();
    }
    for (ScalarTrack& track : result.clip.scalar_tracks) {
        track.sort_keys();
    }
    result.clip.fps = 30.0f;
    result.ok = true;
    return result;
}

VmdResult load_vmd(const std::string& path) {
    VmdResult result;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.error = "could not open VMD file: " + path;
        return result;
    }
    const std::vector<std::uint8_t> buffer(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (buffer.empty()) {
        result.error = "VMD file is empty: " + path;
        return result;
    }

    return parse_vmd(buffer.data(), buffer.size());
}

}  // namespace ms
