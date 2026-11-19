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

#include "formats/PmxLoader.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "core/RigidBody.hpp"

namespace ms {

namespace {

// ---------------------------------------------------------------------------
// PMX constants
// ---------------------------------------------------------------------------

// Text encoding ids from the globals header.
constexpr std::uint8_t kEncodingUtf16Le = 0;
constexpr std::uint8_t kEncodingUtf8 = 1;

// Vertex weight deform types.
constexpr std::uint8_t kWeightBdef1 = 0;
constexpr std::uint8_t kWeightBdef2 = 1;
constexpr std::uint8_t kWeightBdef4 = 2;
constexpr std::uint8_t kWeightSdef = 3;
constexpr std::uint8_t kWeightQdef = 4;

// Material drawing flags.
constexpr std::uint8_t kMaterialNoCull = 0x01;
constexpr std::uint8_t kMaterialHasEdge = 0x10;

// Bone flags, spread over two bytes.
constexpr std::uint16_t kBoneTailIsIndex = 0x0001;
constexpr std::uint16_t kBoneInheritRotation = 0x0100;
constexpr std::uint16_t kBoneInheritTranslation = 0x0200;
constexpr std::uint16_t kBoneFixedAxis = 0x0400;
constexpr std::uint16_t kBoneLocalAxis = 0x0800;
constexpr std::uint16_t kBoneExternalParent = 0x2000;
constexpr std::uint16_t kBoneIk = 0x0020;

// Morph type ids from the morph section.
constexpr std::uint8_t kMorphGroup = 0;
constexpr std::uint8_t kMorphVertex = 1;
constexpr std::uint8_t kMorphBone = 2;
constexpr std::uint8_t kMorphUv = 3;
constexpr std::uint8_t kMorphUvExt1 = 4;
constexpr std::uint8_t kMorphUvExt2 = 5;
constexpr std::uint8_t kMorphUvExt3 = 6;
constexpr std::uint8_t kMorphUvExt4 = 7;
constexpr std::uint8_t kMorphMaterial = 8;
constexpr std::uint8_t kMorphFlip = 9;
constexpr std::uint8_t kMorphImpulse = 10;

// Display frame type ids. Only used to walk past the section.
constexpr std::uint8_t kFrameBone = 0;
constexpr std::uint8_t kFrameMorph = 1;

// ---------------------------------------------------------------------------
// Bounds-checked little-endian reader
//
// Every read validates that the bytes are present, so a truncated or hostile
// file yields a clean error instead of reading out of bounds. Multi-byte
// values are assembled byte by byte rather than memcpy'd, which keeps the
// parser independent of host endianness.
// ---------------------------------------------------------------------------

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

    std::size_t remaining() const { return size_ - offset_; }

    bool fail(const std::string& what) {
        if (ok_) {
            error_ = what;
            ok_ = false;
        }
        return false;
    }

    bool skip(std::size_t count) {
        if (!ok_) {
            return false;
        }
        if (count > remaining()) {
            return fail("unexpected end of file");
        }
        offset_ += count;
        return true;
    }

    bool u8(std::uint8_t& out) {
        if (!ok_) {
            return false;
        }
        if (remaining() < 1) {
            return fail("unexpected end of file");
        }
        out = data_[offset_++];
        return true;
    }

    bool i8(std::int8_t& out) {
        std::uint8_t raw = 0;
        if (!u8(raw)) {
            return false;
        }
        out = static_cast<std::int8_t>(raw);
        return true;
    }

    bool u16(std::uint16_t& out) {
        if (!ok_) {
            return false;
        }
        if (remaining() < 2) {
            return fail("unexpected end of file");
        }
        out = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data_[offset_]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(data_[offset_ + 1]) << 8));
        offset_ += 2;
        return true;
    }

    bool i16(std::int16_t& out) {
        std::uint16_t raw = 0;
        if (!u16(raw)) {
            return false;
        }
        out = static_cast<std::int16_t>(raw);
        return true;
    }

    bool u32(std::uint32_t& out) {
        if (!ok_) {
            return false;
        }
        if (remaining() < 4) {
            return fail("unexpected end of file");
        }
        out = static_cast<std::uint32_t>(data_[offset_]) |
              (static_cast<std::uint32_t>(data_[offset_ + 1]) << 8) |
              (static_cast<std::uint32_t>(data_[offset_ + 2]) << 16) |
              (static_cast<std::uint32_t>(data_[offset_ + 3]) << 24);
        offset_ += 4;
        return true;
    }

    bool i32(std::int32_t& out) {
        std::uint32_t raw = 0;
        if (!u32(raw)) {
            return false;
        }
        out = static_cast<std::int32_t>(raw);
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

    bool vec2f(vec2& out) { return f32(out.x) && f32(out.y); }

    bool vec3f(vec3& out) { return f32(out.x) && f32(out.y) && f32(out.z); }

    bool vec4f(vec4& out) {
        return f32(out.x) && f32(out.y) && f32(out.z) && f32(out.w);
    }

    // A PMX "index" is 1, 2 or 4 bytes wide. Vertex indices are unsigned;
    // bone / texture / material indices are signed so that -1 can mean "none".
    bool index(int size, bool is_signed, std::int32_t& out) {
        switch (size) {
            case 1: {
                if (is_signed) {
                    std::int8_t v = 0;
                    if (!i8(v)) {
                        return false;
                    }
                    out = v;
                } else {
                    std::uint8_t v = 0;
                    if (!u8(v)) {
                        return false;
                    }
                    out = static_cast<std::int32_t>(v);
                }
                return true;
            }
            case 2: {
                if (is_signed) {
                    std::int16_t v = 0;
                    if (!i16(v)) {
                        return false;
                    }
                    out = v;
                } else {
                    std::uint16_t v = 0;
                    if (!u16(v)) {
                        return false;
                    }
                    out = static_cast<std::int32_t>(v);
                }
                return true;
            }
            case 4:
                return i32(out);
            default:
                return fail("invalid index size in header");
        }
    }

    // PMX `text`: a signed byte length followed by that many bytes. The bytes
    // are UTF-16LE or UTF-8 depending on the file's global encoding flag.
    bool text(std::string& out, std::uint8_t encoding) {
        std::int32_t length = 0;
        if (!i32(length)) {
            return false;
        }
        if (length < 0) {
            return fail("negative text length");
        }
        const std::size_t count = static_cast<std::size_t>(length);
        if (count > remaining()) {
            return fail("text runs past end of file");
        }
        const std::uint8_t* bytes = data_ + offset_;
        offset_ += count;

        if (encoding == kEncodingUtf8) {
            out.assign(reinterpret_cast<const char*>(bytes), count);
        } else {
            out = utf16le_to_utf8(bytes, count);
        }
        return true;
    }

    // Rejects counts that cannot possibly fit in the remaining bytes, so a
    // corrupt header cannot make us allocate gigabytes.
    bool count(std::int32_t value, std::size_t min_bytes_each,
               const char* what, std::size_t& out) {
        if (value < 0) {
            return fail(std::string("negative ") + what + " count");
        }
        const std::size_t count_value = static_cast<std::size_t>(value);
        if (min_bytes_each > 0 && count_value > remaining() / min_bytes_each) {
            return fail(std::string(what) + " count exceeds remaining data");
        }
        out = count_value;
        return true;
    }

private:
    static std::string utf16le_to_utf8(const std::uint8_t* bytes,
                                       std::size_t length) {
        std::string out;
        out.reserve(length);
        for (std::size_t i = 0; i + 1 < length; i += 2) {
            std::uint32_t cp =
                static_cast<std::uint32_t>(bytes[i]) |
                (static_cast<std::uint32_t>(bytes[i + 1]) << 8);

            // Combine a surrogate pair into one code point when present.
            if (cp >= 0xD800u && cp <= 0xDBFFu && i + 3 < length) {
                const std::uint32_t low =
                    static_cast<std::uint32_t>(bytes[i + 2]) |
                    (static_cast<std::uint32_t>(bytes[i + 3]) << 8);
                if (low >= 0xDC00u && low <= 0xDFFFu) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                    i += 2;
                }
            }

            if (cp < 0x80u) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800u) {
                out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            } else if (cp < 0x10000u) {
                out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            } else {
                out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
                out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            }
        }
        return out;
    }

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
    bool ok_ = true;
    std::string error_;
};

struct Globals {
    std::uint8_t encoding = kEncodingUtf16Le;
    int additional_vec4 = 0;
    int vertex_index_size = 4;
    int texture_index_size = 4;
    int material_index_size = 4;
    int bone_index_size = 4;
    int morph_index_size = 4;
    int rigidbody_index_size = 4;
};

// MMD is a left-handed, Y-up, Z-forward system; OpenGL/GLM is right-handed.
// Mirroring Z converts between them. PMX stores clockwise-wound front faces in
// that left-handed space; mirroring Z flips each triangle's orientation, so the
// indices must be swapped back to counter-clockwise to match the renderer's
// SG_FACEWINDING_CCW front face. Without the swap the front faces are culled
// and the model renders inside-out. (Verified by the signed volume of the
// mirrored mesh: it is negative as loaded and positive once the swap is
// applied.)
vec3 to_right_handed(const vec3& v) { return vec3(v.x, v.y, -v.z); }

// The same mirror applied to a rotation quaternion (x, y, z, w) is
// (-x, -y, z, w): conjugating the rotation matrix by diag(1, 1, -1) stays a
// proper rotation. This matches VmdLoader's mirror_rotation.
quat rotation_to_right_handed(const quat& q) {
    return glm::normalize(quat(q.w, -q.x, -q.y, q.z));
}

bool read_header(Reader& r, Globals& globals, std::string& model_name) {
    std::uint8_t signature[4] = {0, 0, 0, 0};
    for (std::uint8_t& byte : signature) {
        if (!r.u8(byte)) {
            return false;
        }
    }
    if (std::memcmp(signature, "PMX ", 4) != 0) {
        return r.fail("not a PMX file (bad signature)");
    }

    float version = 0.0f;
    if (!r.f32(version)) {
        return false;
    }
    // 2.0 and 2.1 share a layout up to the bone section; the version only
    // gates extra sections and a couple of optional flags.
    if (version < 1.9f || version > 2.2f) {
        return r.fail("unsupported PMX version");
    }

    std::uint8_t globals_count = 0;
    if (!r.u8(globals_count)) {
        return false;
    }
    if (globals_count < 8) {
        return r.fail("truncated PMX globals header");
    }

    std::uint8_t values[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (std::uint8_t& value : values) {
        if (!r.u8(value)) {
            return false;
        }
    }
    // A file may declare more than 8 globals; skip whatever we do not know.
    if (globals_count > 8 && !r.skip(globals_count - 8u)) {
        return false;
    }

    globals.encoding = values[0];
    globals.additional_vec4 = values[1];
    globals.vertex_index_size = values[2];
    globals.texture_index_size = values[3];
    globals.material_index_size = values[4];
    globals.bone_index_size = values[5];
    globals.morph_index_size = values[6];
    globals.rigidbody_index_size = values[7];

    if (globals.encoding != kEncodingUtf16Le &&
        globals.encoding != kEncodingUtf8) {
        return r.fail("unknown text encoding");
    }
    if (globals.additional_vec4 < 0 || globals.additional_vec4 > 4) {
        return r.fail("invalid additional vec4 count");
    }
    const int sizes[6] = {globals.vertex_index_size, globals.texture_index_size,
                          globals.material_index_size, globals.bone_index_size,
                          globals.morph_index_size,
                          globals.rigidbody_index_size};
    for (int size : sizes) {
        if (size != 1 && size != 2 && size != 4) {
            return r.fail("invalid index size in header");
        }
    }

    std::string name_local;
    std::string name_universal;
    std::string comment_local;
    std::string comment_universal;
    if (!r.text(name_local, globals.encoding) ||
        !r.text(name_universal, globals.encoding) ||
        !r.text(comment_local, globals.encoding) ||
        !r.text(comment_universal, globals.encoding)) {
        return false;
    }
    model_name = name_universal.empty() ? name_local : name_universal;
    return true;
}

bool read_vertices(Reader& r, const Globals& globals, MeshData& mesh) {
    std::size_t count = 0;
    std::int32_t declared = 0;
    if (!r.i32(declared)) {
        return false;
    }
    // Smallest possible vertex is BDEF1 with 1-byte indices.
    if (!r.count(declared, 1, "vertex", count)) {
        return false;
    }

    mesh.vertices.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Vertex vertex;

        vec3 position;
        vec3 normal;
        vec2 uv;
        if (!r.vec3f(position) || !r.vec3f(normal) || !r.vec2f(uv)) {
            return false;
        }
        vertex.position = to_right_handed(position);
        vertex.normal = to_right_handed(normal);
        vertex.uv = uv;

        if (globals.additional_vec4 > 0 &&
            !r.skip(static_cast<std::size_t>(globals.additional_vec4) * 16u)) {
            return false;
        }

        std::uint8_t weight_type = 0;
        if (!r.u8(weight_type)) {
            return false;
        }

        // Reads one bone index, then stores it with its weight. `nil` (-1)
        // influences are dropped, which is how BDEF1/2 leave slots unused.
        const int index_size = globals.bone_index_size;
        const auto read_bone = [&](int slot, float weight) -> bool {
            std::int32_t bone = -1;
            if (!r.index(index_size, true, bone)) {
                return false;
            }
            if (bone >= 0 && weight > 0.0f) {
                vertex.set_influence(slot, static_cast<std::uint32_t>(bone),
                                     weight);
            }
            return true;
        };

        switch (weight_type) {
            case kWeightBdef1: {
                if (!read_bone(0, 1.0f)) {
                    return false;
                }
                break;
            }
            case kWeightBdef2:
            case kWeightSdef: {
                std::int32_t first = -1;
                std::int32_t second = -1;
                float weight = 0.0f;
                if (!r.index(index_size, true, first) ||
                    !r.index(index_size, true, second) || !r.f32(weight)) {
                    return false;
                }
                // PMX stores the first bone's weight as a 4-byte IEEE-754
                // float (unlike the older PMD format's [0,100] byte); the
                // second bone takes the remainder.
                if (first >= 0 && weight > 0.0f) {
                    vertex.set_influence(0, static_cast<std::uint32_t>(first),
                                         weight);
                }
                if (second >= 0 && weight < 1.0f) {
                    vertex.set_influence(1, static_cast<std::uint32_t>(second),
                                         1.0f - weight);
                }
                // SDEF adds the spherical-blend parameters C, R0 and R1, three
                // vec3s (36 bytes) that only matter to a dedicated SDEF
                // evaluator. They are read here to advance the stream exactly;
                // the BDEF2 weights above are the skinning fallback.
                if (weight_type == kWeightSdef) {
                    vec3 sdef_c;
                    vec3 sdef_r0;
                    vec3 sdef_r1;
                    if (!r.vec3f(sdef_c) || !r.vec3f(sdef_r0) ||
                        !r.vec3f(sdef_r1)) {
                        return false;
                    }
                }
                break;
            }
            case kWeightBdef4:
            case kWeightQdef: {
                std::int32_t bones[4] = {-1, -1, -1, -1};
                float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                for (int k = 0; k < 4; ++k) {
                    if (!r.index(index_size, true, bones[k])) {
                        return false;
                    }
                }
                for (float& w : weights) {
                    if (!r.f32(w)) {
                        return false;
                    }
                }
                for (int k = 0; k < 4; ++k) {
                    if (bones[k] >= 0 && weights[k] > 0.0f) {
                        vertex.set_influence(k,
                                             static_cast<std::uint32_t>(bones[k]),
                                             weights[k]);
                    }
                }
                break;
            }
            default:
                return r.fail("unknown vertex weight deform type");
        }

        float edge_scale = 0.0f;
        if (!r.f32(edge_scale)) {
            return false;
        }
        mesh.vertices.push_back(vertex);
    }
    return true;
}

bool read_surfaces(Reader& r, const Globals& globals, MeshData& mesh) {
    std::size_t count = 0;
    std::int32_t declared = 0;
    if (!r.i32(declared)) {
        return false;
    }
    if (!r.count(declared, static_cast<std::size_t>(globals.vertex_index_size),
                 "surface", count)) {
        return false;
    }
    if (count % 3u != 0u) {
        return r.fail("surface count is not a multiple of three");
    }

    mesh.indices.reserve(count);
    const std::int32_t vertex_count =
        static_cast<std::int32_t>(mesh.vertices.size());
    // The Z-mirror in read_vertices flips every triangle's orientation, so a
    // stored triangle (v0, v1, v2) becomes counter-clockwise only once its last
    // two vertices are swapped. Emit them in that order to match the CCW front
    // face the pipelines use.
    for (std::size_t i = 0; i < count; i += 3) {
        std::int32_t tri[3] = {0, 0, 0};
        for (std::int32_t& index : tri) {
            if (!r.index(globals.vertex_index_size, false, index)) {
                return false;
            }
            if (index < 0 || index >= vertex_count) {
                return r.fail("surface references a vertex out of range");
            }
        }
        mesh.indices.push_back(static_cast<std::uint32_t>(tri[0]));
        mesh.indices.push_back(static_cast<std::uint32_t>(tri[2]));
        mesh.indices.push_back(static_cast<std::uint32_t>(tri[1]));
    }
    return true;
}

bool read_textures(Reader& r, const Globals& globals,
                   std::vector<std::string>& paths) {
    std::size_t count = 0;
    std::int32_t declared = 0;
    if (!r.i32(declared)) {
        return false;
    }
    // Each path is at least a 4-byte length prefix.
    if (!r.count(declared, 4, "texture", count)) {
        return false;
    }
    paths.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::string path;
        if (!r.text(path, globals.encoding)) {
            return false;
        }
        paths.push_back(std::move(path));
    }
    return true;
}

bool read_materials(Reader& r, const Globals& globals,
                    const std::vector<std::string>& texture_paths,
                    std::vector<Material>& materials) {
    std::size_t count = 0;
    std::int32_t declared = 0;
    if (!r.i32(declared)) {
        return false;
    }
    if (!r.count(declared, 4, "material", count)) {
        return false;
    }

    const int texture_count = static_cast<int>(texture_paths.size());
    std::uint32_t offset = 0;
    materials.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Material material;

        std::string name_local;
        std::string name_universal;
        vec4 diffuse;
        vec3 specular;
        float specular_strength = 0.0f;
        vec3 ambient;
        std::uint8_t flags = 0;
        vec4 edge_color;
        float edge_scale = 0.0f;
        std::int32_t texture_index = -1;
        std::int32_t environment_index = -1;
        std::uint8_t environment_blend = 0;
        std::uint8_t toon_reference = 0;
        std::string metadata;
        std::int32_t surface_count = 0;

        if (!r.text(name_local, globals.encoding) ||
            !r.text(name_universal, globals.encoding) ||
            !r.vec4f(diffuse) || !r.vec3f(specular) ||
            !r.f32(specular_strength) || !r.vec3f(ambient) || !r.u8(flags) ||
            !r.vec4f(edge_color) || !r.f32(edge_scale) ||
            !r.index(globals.texture_index_size, true, texture_index) ||
            !r.index(globals.texture_index_size, true, environment_index) ||
            !r.u8(environment_blend) || !r.u8(toon_reference)) {
            return false;
        }

        // The toon value is a texture index or a raw byte, depending on the
        // reference mode. Either way it must be consumed to stay aligned.
        if (toon_reference == 0) {
            std::int32_t toon_index = -1;
            if (!r.index(globals.texture_index_size, true, toon_index)) {
                return false;
            }
        } else {
            std::uint8_t toon_byte = 0;
            if (!r.u8(toon_byte)) {
                return false;
            }
        }

        if (!r.text(metadata, globals.encoding) || !r.i32(surface_count)) {
            return false;
        }
        if (surface_count < 0 || surface_count % 3 != 0) {
            return r.fail("material surface count is invalid");
        }

        material.name = name_universal.empty() ? name_local : name_universal;
        material.diffuse = diffuse;
        material.specular = specular;
        material.shininess = specular_strength;
        material.ambient = ambient;
        material.double_sided = (flags & kMaterialNoCull) != 0;
        material.has_edges = (flags & kMaterialHasEdge) != 0;
        material.texture =
            (texture_index >= 0 && texture_index < texture_count)
                ? texture_index
                : -1;
        material.index_offset = offset;
        material.index_count = static_cast<std::uint32_t>(surface_count);
        offset += static_cast<std::uint32_t>(surface_count);

        materials.push_back(std::move(material));
    }
    return true;
}

bool read_bones(Reader& r, const Globals& globals, Skeleton& skeleton) {
    std::size_t count = 0;
    std::int32_t declared = 0;
    if (!r.i32(declared)) {
        return false;
    }
    if (!r.count(declared, 4, "bone", count)) {
        return false;
    }

    skeleton.bones().reserve(count);

    // IK solvers are collected as the bones are read, then appended to the
    // skeleton. Each solver remembers which bone owns it.
    struct PendingIk {
        int bone = -1;
        IkSolver solver;
    };
    std::vector<PendingIk> pending_ik;

    for (std::size_t i = 0; i < count; ++i) {
        std::string name_local;
        std::string name_universal;
        vec3 position;
        std::int32_t parent = -1;
        std::int32_t layer = 0;
        std::uint8_t flags_low = 0;
        std::uint8_t flags_high = 0;

        if (!r.text(name_local, globals.encoding) ||
            !r.text(name_universal, globals.encoding) || !r.vec3f(position) ||
            !r.index(globals.bone_index_size, true, parent) || !r.i32(layer) ||
            !r.u8(flags_low) || !r.u8(flags_high)) {
            return false;
        }
        const std::uint16_t flags = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(flags_low) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(flags_high) << 8));

        // Tail position: a vector, or a bone index when the flag says so.
        if ((flags & kBoneTailIsIndex) != 0) {
            std::int32_t tail = -1;
            if (!r.index(globals.bone_index_size, true, tail)) {
                return false;
            }
        } else {
            vec3 tail;
            if (!r.vec3f(tail)) {
                return false;
            }
        }

        // Append/Grant ("inherit"): a parent bone index and a weight in [0,1].
        bool has_append = false;
        std::int32_t append_parent = -1;
        float append_weight = 0.0f;
        if ((flags & (kBoneInheritRotation | kBoneInheritTranslation)) != 0) {
            if (!r.index(globals.bone_index_size, true, append_parent) ||
                !r.f32(append_weight)) {
                return false;
            }
            has_append = true;
        }
        if ((flags & kBoneFixedAxis) != 0) {
            vec3 axis;
            if (!r.vec3f(axis)) {
                return false;
            }
        }
        if ((flags & kBoneLocalAxis) != 0) {
            vec3 local_x;
            vec3 local_z;
            if (!r.vec3f(local_x) || !r.vec3f(local_z)) {
                return false;
            }
        }
        if ((flags & kBoneExternalParent) != 0) {
            // Documented as an index, but in practice a plain 4-byte int.
            std::int32_t external_parent = 0;
            if (!r.i32(external_parent)) {
                return false;
            }
        }

        // IK: target bone, iteration count, per-step limit angle, and links.
        // Collected here and turned into a solver after the bone exists.
        IkSolver ik;
        bool has_ik = false;
        if ((flags & kBoneIk) != 0) {
            std::int32_t target = -1;
            std::int32_t loop_count = 0;
            float limit_radian = 0.0f;
            std::int32_t link_count = 0;
            if (!r.index(globals.bone_index_size, true, target) ||
                !r.i32(loop_count) || !r.f32(limit_radian) ||
                !r.i32(link_count) || link_count < 0) {
                return false;
            }
            has_ik = true;
            // PMX's "target index" is the chain end the solver moves, and the
            // bone carrying the IK flag is the controller it moves onto.
            ik.effector_bone = target;
            ik.iterations = (loop_count > 0) ? loop_count : 1;
            ik.limit_angle = limit_radian;
            ik.links.reserve(static_cast<std::size_t>(link_count));
            for (std::int32_t link = 0; link < link_count; ++link) {
                std::int32_t link_bone = -1;
                std::uint8_t has_limits = 0;
                if (!r.index(globals.bone_index_size, true, link_bone) ||
                    !r.u8(has_limits)) {
                    return false;
                }
                IkLink ik_link;
                ik_link.bone = link_bone;
                if (has_limits != 0) {
                    vec3 limit_min;
                    vec3 limit_max;
                    if (!r.vec3f(limit_min) || !r.vec3f(limit_max)) {
                        return false;
                    }
                    // Limits are signed radians on each local axis, and the
                    // mirror flips X and Y, so both bounds negate (saba does
                    // the same in PMXModel).
                    ik_link.enable_limit = true;
                    ik_link.limit_min = -limit_max;
                    ik_link.limit_max = -limit_min;
                }
                ik.links.push_back(ik_link);
            }
        }

        // Only a parent that already exists can be linked, which also
        // guarantees the hierarchy stays acyclic.
        const int parent_index =
            (parent >= 0 && parent < static_cast<std::int32_t>(i)) ? parent : -1;
        const int bone_index = skeleton.add_bone(
            name_universal.empty() ? name_local : name_universal, parent_index,
            to_right_handed(position));

        Bone& bone = skeleton.bone(bone_index);
        bone.deform_depth = layer;
        if (has_append) {
            bone.has_append = true;
            bone.append_parent = append_parent;
            bone.append_weight = append_weight;
            bone.append_rotate = (flags & kBoneInheritRotation) != 0;
            bone.append_translate = (flags & kBoneInheritTranslation) != 0;
        }
        if (has_ik) {
            ik.goal_bone = bone_index;
            pending_ik.push_back(PendingIk{bone_index, std::move(ik)});
        }
    }

    // Materialise the IK solvers and link each to the bone carrying its flag.
    // A chain end that does not exist would make the solver a no-op, so it is
    // dropped.
    for (PendingIk& pending : pending_ik) {
        if (pending.solver.effector_bone < 0 ||
            pending.solver.effector_bone >= static_cast<std::int32_t>(count)) {
            continue;
        }
        skeleton.ik_solvers().push_back(std::move(pending.solver));
        skeleton.bone(pending.bone).ik_solver =
            static_cast<int>(skeleton.ik_solvers().size()) - 1;
    }
    return true;
}

// Reads the morph section. Vertex, bone and group morphs are stored; every
// other morph type (UV, material, flip, impulse) is parsed far enough to know
// its byte size and then skipped, so a real file still parses completely and
// the reader never misaligns.
bool read_morphs(Reader& r, const Globals& globals, MorphSet& morphs) {
    std::size_t count = 0;
    std::int32_t declared = 0;
    if (!r.i32(declared)) {
        return false;
    }
    if (!r.count(declared, 12, "morph", count)) {
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        std::string name_local;
        std::string name_universal;
        std::uint8_t panel = 0;
        std::uint8_t type = 0;
        std::int32_t offset_count = 0;
        if (!r.text(name_local, globals.encoding) ||
            !r.text(name_universal, globals.encoding) || !r.u8(panel) ||
            !r.u8(type) || !r.i32(offset_count) || offset_count < 0) {
            return false;
        }
        const std::size_t n = static_cast<std::size_t>(offset_count);

        MorphTarget target;
        target.name = name_universal.empty() ? name_local : name_universal;

        switch (type) {
            case kMorphGroup: {
                target.kind = MorphTarget::Kind::Group;
                target.group_refs.reserve(n);
                for (std::size_t k = 0; k < n; ++k) {
                    std::int32_t ref = -1;
                    float influence = 0.0f;
                    if (!r.index(globals.morph_index_size, true, ref) ||
                        !r.f32(influence)) {
                        return false;
                    }
                    if (ref >= 0) {
                        target.group_refs.emplace_back(
                            static_cast<std::uint32_t>(ref), influence);
                    }
                }
                break;
            }
            case kMorphVertex: {
                target.kind = MorphTarget::Kind::Vertex;
                target.vertex_offsets.reserve(n);
                for (std::size_t k = 0; k < n; ++k) {
                    std::int32_t vertex = -1;
                    vec3 offset;
                    if (!r.index(globals.vertex_index_size, false, vertex) ||
                        !r.vec3f(offset)) {
                        return false;
                    }
                    VertexMorphOffset v;
                    v.vertex = static_cast<std::uint32_t>(vertex);
                    v.offset = to_right_handed(offset);
                    target.vertex_offsets.push_back(v);
                }
                break;
            }
            case kMorphBone: {
                target.kind = MorphTarget::Kind::Bone;
                target.bone_offsets.reserve(n);
                for (std::size_t k = 0; k < n; ++k) {
                    std::int32_t bone = -1;
                    vec3 translation;
                    vec4 rotation;
                    if (!r.index(globals.bone_index_size, true, bone) ||
                        !r.vec3f(translation) || !r.vec4f(rotation)) {
                        return false;
                    }
                    BoneMorphOffset b;
                    b.bone = static_cast<std::uint32_t>(bone);
                    b.translation = to_right_handed(translation);
                    b.rotation = rotation_to_right_handed(
                        quat(rotation.w, rotation.x, rotation.y, rotation.z));
                    target.bone_offsets.push_back(b);
                }
                break;
            }
            case kMorphUv:
            case kMorphUvExt1:
            case kMorphUvExt2:
            case kMorphUvExt3:
            case kMorphUvExt4: {
                // vertex index + vec4
                for (std::size_t k = 0; k < n; ++k) {
                    std::int32_t vertex = -1;
                    vec4 uv;
                    if (!r.index(globals.vertex_index_size, false, vertex) ||
                        !r.vec4f(uv)) {
                        return false;
                    }
                }
                continue;
            }
            case kMorphMaterial: {
                // material index + op byte + diffuse(4) specular(3)
                // shininess(1) ambient(3) edgeColor(4) edgeSize(1)
                // texture(4) sphere(4) toon(4)
                for (std::size_t k = 0; k < n; ++k) {
                    std::int32_t material = -1;
                    std::uint8_t op = 0;
                    vec4 diffuse;
                    vec3 specular;
                    float shininess = 0.0f;
                    vec3 ambient;
                    vec4 edge_color;
                    float edge_size = 0.0f;
                    vec4 tint;
                    if (!r.index(globals.material_index_size, true, material) ||
                        !r.u8(op) || !r.vec4f(diffuse) || !r.vec3f(specular) ||
                        !r.f32(shininess) || !r.vec3f(ambient) ||
                        !r.vec4f(edge_color) || !r.f32(edge_size) ||
                        !r.vec4f(tint) || !r.vec4f(tint) || !r.vec4f(tint)) {
                        return false;
                    }
                }
                continue;
            }
            case kMorphFlip: {
                // morph index + weight (same shape as a group entry)
                for (std::size_t k = 0; k < n; ++k) {
                    std::int32_t ref = -1;
                    float influence = 0.0f;
                    if (!r.index(globals.morph_index_size, true, ref) ||
                        !r.f32(influence)) {
                        return false;
                    }
                }
                continue;
            }
            case kMorphImpulse: {
                // rigid body index + local flag + two vec3
                for (std::size_t k = 0; k < n; ++k) {
                    std::int32_t body = -1;
                    std::uint8_t local = 0;
                    vec3 speed;
                    vec3 torque;
                    if (!r.index(globals.rigidbody_index_size, true, body) ||
                        !r.u8(local) || !r.vec3f(speed) || !r.vec3f(torque)) {
                        return false;
                    }
                }
                continue;
            }
            default:
                return r.fail("unknown morph type");
        }

        morphs.add(std::move(target));
    }
    return true;
}

// Reads the display-frame section and discards it. Its size depends on the
// frame type, so it must be walked exactly or the rigid-body section that
// follows would be read from the wrong offset.
bool read_display_frames(Reader& r, const Globals& globals) {
    std::size_t count = 0;
    std::int32_t declared = 0;
    if (!r.i32(declared)) {
        return false;
    }
    if (!r.count(declared, 1, "display frame", count)) {
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        std::string name_local;
        std::string name_universal;
        std::uint8_t special = 0;
        std::int32_t frame_count = 0;
        if (!r.text(name_local, globals.encoding) ||
            !r.text(name_universal, globals.encoding) || !r.u8(special) ||
            !r.i32(frame_count) || frame_count < 0) {
            return false;
        }
        const std::size_t frames = static_cast<std::size_t>(frame_count);
        for (std::size_t f = 0; f < frames; ++f) {
            std::uint8_t type = 0;
            std::int32_t index = -1;
            if (!r.u8(type)) {
                return false;
            }
            switch (type) {
                case kFrameBone:
                    if (!r.index(globals.bone_index_size, true, index)) {
                        return false;
                    }
                    break;
                case kFrameMorph:
                    if (!r.index(globals.morph_index_size, true, index)) {
                        return false;
                    }
                    break;
                default:
                    return r.fail("unknown display frame type");
            }
        }
    }
    return true;
}

// Reads the rigid-body section. Positions and rotations are model-space in
// PMX (the body's offset from its bone is derived), so only the Z-mirror is
// applied — the same conversion the mesh and bones get. Shape sizes are local
// extents and are left alone.
bool read_rigid_bodies(Reader& r, const Globals& globals, RigidBodySet& set) {
    std::size_t count = 0;
    std::int32_t declared = 0;
    if (!r.i32(declared)) {
        return false;
    }
    // Smallest rigid body is ~32 bytes; 16 is a safe lower bound for the guard.
    if (!r.count(declared, 16, "rigid body", count)) {
        return false;
    }

    set.bodies.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::string name_local;
        std::string name_universal;
        std::int32_t bone = -1;
        std::uint8_t group = 0;
        std::uint16_t mask = 0;
        std::uint8_t shape = 0;
        vec3 size;
        vec3 position;
        vec3 rotation;
        float mass = 0.0f;
        float move_attenuation = 0.0f;
        float rotation_damping = 0.0f;
        float repulsion = 0.0f;
        float friction = 0.0f;
        std::uint8_t mode = 0;
        if (!r.text(name_local, globals.encoding) ||
            !r.text(name_universal, globals.encoding) ||
            !r.index(globals.bone_index_size, true, bone) || !r.u8(group) ||
            !r.u16(mask) || !r.u8(shape) || !r.vec3f(size) ||
            !r.vec3f(position) || !r.vec3f(rotation) || !r.f32(mass) ||
            !r.f32(move_attenuation) || !r.f32(rotation_damping) ||
            !r.f32(repulsion) || !r.f32(friction) || !r.u8(mode)) {
            return false;
        }
        if (shape > static_cast<std::uint8_t>(RigidBodyShape::Capsule)) {
            return r.fail("unknown rigid body shape");
        }
        if (mode > static_cast<std::uint8_t>(RigidBodyMode::DynamicAndBone)) {
            return r.fail("unknown rigid body mode");
        }

        RigidBody body;
        body.name = name_universal.empty() ? name_local : name_universal;
        body.bone = bone;
        body.group = group;
        body.collision_mask = mask;
        body.shape = static_cast<RigidBodyShape>(shape);
        body.size = size;
        body.position = to_right_handed(position);
        body.rotation = rotation_to_right_handed(
            euler_degrees_to_quat(glm::degrees(rotation)));
        body.mass = mass;
        body.linear_damping = move_attenuation;
        body.angular_damping = rotation_damping;
        body.restitution = repulsion;
        body.friction = friction;
        body.mode = static_cast<RigidBodyMode>(mode);
        set.bodies.push_back(std::move(body));
    }
    return true;
}

// Reads the joint section. A joint's frame is model-space, so the position and
// the orientation take the Z-mirror; its limits are expressed along the joint's
// own (already mirrored) axes and are therefore kept as written.
bool read_joints(Reader& r, const Globals& globals, RigidBodySet& set) {
    std::size_t count = 0;
    std::int32_t declared = 0;
    if (!r.i32(declared)) {
        return false;
    }
    if (!r.count(declared, 32, "joint", count)) {
        return false;
    }

    set.joints.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::string name_local;
        std::string name_universal;
        std::uint8_t type = 0;
        std::int32_t body_a = -1;
        std::int32_t body_b = -1;
        vec3 position;
        vec3 rotation;
        vec3 translation_min;
        vec3 translation_max;
        vec3 rotation_min;
        vec3 rotation_max;
        vec3 spring_translation;
        vec3 spring_rotation;
        if (!r.text(name_local, globals.encoding) ||
            !r.text(name_universal, globals.encoding) || !r.u8(type) ||
            !r.index(globals.rigidbody_index_size, true, body_a) ||
            !r.index(globals.rigidbody_index_size, true, body_b) ||
            !r.vec3f(position) || !r.vec3f(rotation) ||
            !r.vec3f(translation_min) || !r.vec3f(translation_max) ||
            !r.vec3f(rotation_min) || !r.vec3f(rotation_max) ||
            !r.vec3f(spring_translation) || !r.vec3f(spring_rotation)) {
            return false;
        }
        if (type > static_cast<std::uint8_t>(JointType::Hinge)) {
            return r.fail("unknown joint type");
        }

        Joint joint;
        joint.name = name_universal.empty() ? name_local : name_universal;
        joint.type = static_cast<JointType>(type);
        joint.body_a = body_a;
        joint.body_b = body_b;
        joint.position = to_right_handed(position);
        joint.rotation = rotation_to_right_handed(
            euler_degrees_to_quat(glm::degrees(rotation)));
        joint.translation_min = translation_min;
        joint.translation_max = translation_max;
        joint.rotation_min = rotation_min;
        joint.rotation_max = rotation_max;
        joint.spring_translation = spring_translation;
        joint.spring_rotation = spring_rotation;
        set.joints.push_back(std::move(joint));
    }
    return true;
}

}  // namespace

PmxResult parse_pmx(const std::uint8_t* data, std::size_t size) {
    PmxResult result;
    if (data == nullptr || size < 4) {
        result.error = "empty or truncated PMX data";
        return result;
    }

    Reader reader(data, size);
    Globals globals;
    std::string model_name;
    if (!read_header(reader, globals, model_name)) {
        result.error = reader.ok() ? "malformed PMX header" : reader.error();
        return result;
    }

    Model model;
    model.name = model_name;

    if (!read_vertices(reader, globals, model.mesh) ||
        !read_surfaces(reader, globals, model.mesh) ||
        !read_textures(reader, globals, model.texture_paths) ||
        !read_materials(reader, globals, model.texture_paths, model.materials)) {
        result.error = reader.error();
        return result;
    }

    // Material index ranges must tile the index buffer exactly; a mismatch
    // means the file is inconsistent and drawing it would read out of range.
    std::size_t material_indices = 0;
    for (const Material& material : model.materials) {
        material_indices += material.index_count;
    }
    if (material_indices != model.mesh.indices.size()) {
        result.error = "material surface ranges do not match the index buffer";
        return result;
    }

    if (!read_bones(reader, globals, model.skeleton)) {
        result.error = reader.error();
        return result;
    }

    // Section order after the bones is: morphs, display frames, rigid bodies,
    // joints (then 2.1 soft bodies). The display frames only describe the UI,
    // so they are walked and discarded; the rigid bodies and joints are parsed
    // into the model's physics data.
    if (!read_morphs(reader, globals, model.morphs)) {
        result.error = reader.error();
        return result;
    }

    if (!read_display_frames(reader, globals)) {
        result.error = reader.error();
        return result;
    }

    if (!read_rigid_bodies(reader, globals, model.rigid_bodies) ||
        !read_joints(reader, globals, model.rigid_bodies)) {
        result.error = reader.error();
        return result;
    }

    model.skeleton.compute_rest();

    result.ok = true;
    result.model = std::move(model);
    return result;
}

PmxResult load_pmx(const std::string& path) {
    PmxResult result;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        result.error = "cannot open file: " + path;
        return result;
    }
    const std::streamoff end = file.tellg();
    if (end <= 0) {
        result.error = "file is empty: " + path;
        return result;
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(end));
    if (!file.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()))) {
        result.error = "failed to read file: " + path;
        return result;
    }
    return parse_pmx(buffer.data(), buffer.size());
}

}  // namespace ms
