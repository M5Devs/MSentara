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

// Builds a synthetic PMX 2.0 model in memory and checks the parser end to
// end. No copyrighted asset is involved: the bytes below are generated here,
// which also lets the test pin down exact field offsets and exercise the
// UTF-16LE and skinning paths deterministically.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "TestCommon.hpp"
#include "core/Skeleton.hpp"
#include "formats/PmxLoader.hpp"

namespace {

using namespace ms;

// ---------------------------------------------------------------------------
// PMX writer
// ---------------------------------------------------------------------------

// Encodes a UTF-8 literal to UTF-16LE, so the test can feed real multi-byte
// names without hand-computing code units.
std::vector<std::uint8_t> utf8_to_utf16le(const std::string& utf8) {
    std::vector<std::uint32_t> code_points;
    for (std::size_t i = 0; i < utf8.size();) {
        const std::uint8_t lead = static_cast<std::uint8_t>(utf8[i]);
        std::uint32_t cp = 0;
        int extra = 0;
        if (lead < 0x80u) {
            cp = lead;
        } else if ((lead & 0xE0u) == 0xC0u) {
            cp = lead & 0x1Fu;
            extra = 1;
        } else if ((lead & 0xF0u) == 0xE0u) {
            cp = lead & 0x0Fu;
            extra = 2;
        } else {
            cp = lead & 0x07u;
            extra = 3;
        }
        ++i;
        for (int k = 0; k < extra && i < utf8.size(); ++k, ++i) {
            cp = (cp << 6) | (static_cast<std::uint8_t>(utf8[i]) & 0x3Fu);
        }
        code_points.push_back(cp);
    }

    std::vector<std::uint8_t> out;
    for (std::uint32_t cp : code_points) {
        if (cp < 0x10000u) {
            out.push_back(static_cast<std::uint8_t>(cp & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((cp >> 8) & 0xFFu));
        } else {
            const std::uint32_t v = cp - 0x10000u;
            const std::uint32_t high = 0xD800u + (v >> 10);
            const std::uint32_t low = 0xDC00u + (v & 0x3FFu);
            out.push_back(static_cast<std::uint8_t>(high & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((high >> 8) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>(low & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((low >> 8) & 0xFFu));
        }
    }
    return out;
}

class PmxBuilder {
public:
    void u8(std::uint8_t v) { bytes_.push_back(v); }
    void i8(std::int8_t v) { bytes_.push_back(static_cast<std::uint8_t>(v)); }
    void u16(std::uint16_t v) {
        bytes_.push_back(static_cast<std::uint8_t>(v & 0xFFu));
        bytes_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            bytes_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
        }
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void f32(float v) {
        std::uint32_t raw = 0;
        std::memcpy(&raw, &v, sizeof(raw));
        u32(raw);
    }
    void vec2f(const vec2& v) {
        f32(v.x);
        f32(v.y);
    }
    void vec3f(const vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void vec4f(const vec4& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
        f32(v.w);
    }
    void raw(const std::string& s) {
        bytes_.insert(bytes_.end(), s.begin(), s.end());
    }
    void text_utf8(const std::string& s) {
        i32(static_cast<std::int32_t>(s.size()));
        raw(s);
    }
    void text_utf16(const std::string& utf8) {
        const std::vector<std::uint8_t> encoded = utf8_to_utf16le(utf8);
        i32(static_cast<std::int32_t>(encoded.size()));
        bytes_.insert(bytes_.end(), encoded.begin(), encoded.end());
    }

    const std::uint8_t* data() const { return bytes_.data(); }
    std::size_t size() const { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
};

// Writes the header plus every section of a small but complete model.
//
// Two bones: "root" at the origin and "child" at (0,1,0) parented to root.
// Four vertices, one of which is weighted to the child bone so that a pose
// change must visibly move it.
void build_model(PmxBuilder& b, bool utf16) {
    const auto text = [&](const std::string& s) {
        if (utf16) {
            b.text_utf16(s);
        } else {
            b.text_utf8(s);
        }
    };

    // --- Header ---
    b.raw("PMX ");
    b.f32(2.0f);
    b.u8(8);
    // encoding, additional vec4, vertex/texture/material/bone/morph/rigidbody
    // index sizes. One-byte indices keep the synthetic model compact and
    // exercise the narrowest index path.
    b.u8(utf16 ? 0 : 1);
    b.u8(0);
    for (int i = 0; i < 6; ++i) {
        b.u8(1);
    }
    text("テストモデル");  // local name, multi-byte on purpose
    text("Test Model");     // universal name
    text("");               // local comment
    text("");               // universal comment

    // --- Vertices ---
    b.i32(5);
    const auto vertex = [&](const vec3& position, const vec2& uv,
                            std::uint8_t weight_type) {
        b.vec3f(position);
        b.vec3f(vec3(0.0f, 0.0f, 1.0f));
        b.vec2f(uv);
        b.u8(weight_type);
    };

    // Each vertex ends with its own edge-scale float, immediately after the
    // weights. PMX BDEF1 has no weight (the single bone is implicitly 1.0);
    // BDEF2/SDEF carry a 4-byte float weight; BDEF4 carries four floats.

    // V0: BDEF1, fully the child bone, so a rotation of that bone moves it.
    vertex(vec3(1.0f, 1.0f, 0.0f), vec2(0.0f, 0.0f), 0 /* BDEF1 */);
    b.i8(1);     // bone 1
    b.f32(1.0f); // edge scale

    // V1: BDEF2, 25% root / 75% child. The weight is a 4-byte IEEE-754 float.
    vertex(vec3(1.0f, 1.0f, 0.0f), vec2(1.0f, 0.0f), 1 /* BDEF2 */);
    b.i8(0);
    b.i8(1);
    b.f32(0.25f);
    b.f32(1.0f);  // edge scale

    // V2: BDEF4 with an unused (nil) slot.
    vertex(vec3(0.0f, 2.0f, 0.0f), vec2(0.0f, 1.0f), 2 /* BDEF4 */);
    b.i8(0);
    b.i8(1);
    b.i8(-1);
    b.i8(-1);
    b.f32(0.5f);
    b.f32(0.5f);
    b.f32(0.0f);
    b.f32(0.0f);
    b.f32(1.0f);  // edge scale

    // V3: BDEF1, fully the root bone, and off the Z axis to pin the
    // handedness flip.
    vertex(vec3(1.0f, 0.0f, 3.0f), vec2(1.0f, 1.0f), 0 /* BDEF1 */);
    b.i8(0);
    b.f32(1.0f);  // edge scale

    // V4: SDEF, 50/50. After the shared BDEF2 fields it carries the three
    // spherical-blend vec3s C, R0, R1 (36 bytes). The parser reads them to
    // stay aligned; only the weights affect skinning here.
    vertex(vec3(2.0f, 1.0f, 0.0f), vec2(0.5f, 0.5f), 3 /* SDEF */);
    b.i8(0);
    b.i8(1);
    b.f32(0.5f);
    b.vec3f(vec3(0.0f, 1.0f, 0.0f));   // C
    b.vec3f(vec3(1.0f, 0.0f, 0.0f));   // R0
    b.vec3f(vec3(0.0f, 1.0f, 0.0f));   // R1
    b.f32(1.0f);  // edge scale

    // --- Surfaces: two triangles ---
    b.i32(6);
    for (std::uint8_t index : {static_cast<std::uint8_t>(0),
                               static_cast<std::uint8_t>(1),
                               static_cast<std::uint8_t>(2),
                               static_cast<std::uint8_t>(0),
                               static_cast<std::uint8_t>(2),
                               static_cast<std::uint8_t>(3)}) {
        b.u8(index);
    }

    // --- Textures ---
    b.i32(1);
    text("tex/body.png");

    // --- Materials ---
    b.i32(1);
    text("肌");  // local name
    text("Skin");  // universal name
    b.vec4f(vec4(0.8f, 0.2f, 0.2f, 1.0f));  // diffuse
    b.vec3f(vec3(0.1f, 0.1f, 0.1f));        // specular
    b.f32(5.0f);                            // specular strength
    b.vec3f(vec3(0.5f, 0.5f, 0.5f));        // ambient
    b.u8(0x01);                             // no-cull -> double sided
    b.vec4f(vec4(0.0f, 0.0f, 0.0f, 1.0f));  // edge colour
    b.f32(1.0f);                            // edge scale
    b.i8(0);                                // texture index
    b.i8(-1);                               // environment index
    b.u8(0);                                // environment blend mode
    b.u8(1);                                // toon reference: internal
    b.u8(0);                                // toon byte
    text("");                               // metadata
    b.i32(6);                               // surface count

    // --- Bones ---
    b.i32(2);
    // Bone 0: root. Local name is multi-byte; the universal name is empty, so
    // the parser must fall back to the local one.
    text("ルート");
    text("");
    b.vec3f(vec3(0.0f, 0.0f, 0.0f));
    b.i8(-1);  // no parent
    b.i32(0);  // layer
    b.u8(0x00);
    b.u8(0x00);  // flags: tail is a vec3, no IK, no inheritance
    b.vec3f(vec3(0.0f, 1.0f, 0.0f));  // tail

    // Bone 1: child of root.
    text("child");
    text("");
    b.vec3f(vec3(0.0f, 1.0f, 0.0f));
    b.i8(0);   // parent index 0
    b.i32(0);  // layer
    b.u8(0x00);
    b.u8(0x00);
    b.vec3f(vec3(0.0f, 2.0f, 0.0f));  // tail

    // --- Morphs ---
    // Two morphs: "Smile" moves vertex 4 up in model space, and "Combo" is a
    // group that drives "Smile" at half strength.
    b.i32(2);

    text("Smile");
    text("");
    b.u8(1);   // panel: eyebrow
    b.u8(1);   // type: vertex
    b.i32(1);  // one offset
    b.u8(4);   // vertex index (1-byte, unsigned)
    b.vec3f(vec3(0.0f, 2.0f, 0.0f));  // offset (z mirrors to 0)

    text("Combo");
    text("");
    b.u8(1);   // panel
    b.u8(0);   // type: group
    b.i32(1);  // one reference
    b.u8(0);   // morph index 0 (1-byte, signed)
    b.f32(0.5f);

    // Display frames: none. The section is still written, because the parser
    // walks it to reach the rigid bodies that follow.
    b.i32(0);

    // Rigid bodies and joints: none for this model. A model with no physics
    // must still parse, which is the common case for a plain mesh.
    b.i32(0);
    b.i32(0);
}

// A PMX with a three-bone IK chain, laid out the way MMD writes one:
//
//   hip  (0, 1, 0)   root
//   knee (0, 0, 0)   child of hip
//   ankle(0,-1, 0)   child of knee    <- the PMX "target index" (chain end)
//   ikctl(0.6,-0.8,0) root            <- carries the IK flag (the controller)
//
// The solver's target index is the chain end and the flag-carrier is the
// controller, so the loader must map ankle -> effector_bone and ikctl ->
// goal_bone. Getting that backwards makes the chain fly away from its
// controller instead of onto it.
void build_ik_model(PmxBuilder& b) {
    const auto text = [&](const std::string& s) { b.text_utf8(s); };

    b.raw("PMX ");
    b.f32(2.0f);
    b.u8(8);
    b.u8(1);  // UTF-8
    b.u8(0);  // additional vec4
    for (int i = 0; i < 6; ++i) {
        b.u8(1);  // every index size = 1 byte
    }
    text("IKModel");
    text("");
    text("");
    text("");

    b.i32(0);  // vertices
    b.i32(0);  // surfaces
    b.i32(0);  // textures
    b.i32(0);  // materials

    // --- Bones ---
    b.i32(4);
    // hip: root, at (0,1,0).
    text("hip");
    text("");
    b.vec3f(vec3(0.0f, 1.0f, 0.0f));
    b.i8(-1);
    b.i32(0);
    b.u8(0x00);
    b.u8(0x00);  // no flags
    b.vec3f(vec3(0.0f, 0.0f, 0.0f));  // tail

    // knee: child of hip, at (0,0,0).
    text("knee");
    text("");
    b.vec3f(vec3(0.0f, 0.0f, 0.0f));
    b.i8(0);
    b.i32(0);
    b.u8(0x00);
    b.u8(0x00);
    b.vec3f(vec3(0.0f, -1.0f, 0.0f));  // tail

    // ankle: child of knee, at (0,-1,0). This is the solver's chain end.
    text("ankle");
    text("");
    b.vec3f(vec3(0.0f, -1.0f, 0.0f));
    b.i8(1);
    b.i32(0);
    b.u8(0x00);
    b.u8(0x00);
    b.vec3f(vec3(0.0f, -1.0f, 0.0f));  // tail

    // ikctl: root, at (0.6,-0.8,0), carrying the IK flag. Its IK block points
    // the solver at the ankle (bone index 2) with the knee and hip as links.
    text("ikctl");
    text("");
    b.vec3f(vec3(0.6f, -0.8f, 0.0f));
    b.i8(-1);
    b.i32(0);
    // The flag word is little-endian: 0x0020 (kBoneIk) is low byte 0x20.
    b.u8(0x20);  // flags low byte: kBoneIk
    b.u8(0x00);  // flags high byte
    b.vec3f(vec3(0.0f, -1.0f, 0.0f));  // tail
    b.i8(2);        // target index -> ankle (the chain end)
    b.i32(40);      // loop count
    b.f32(0.0f);    // limit radian (no clamp)
    b.i32(2);       // link count
    b.i8(1);        // link 0: knee
    b.u8(0);        // no limits
    b.i8(0);        // link 1: hip
    b.u8(0);        // no limits

    b.i32(0);  // morphs
    b.i32(0);  // display frames
    b.i32(0);  // rigid bodies
    b.i32(0);  // joints
}

// A PMX whose bone and morph carry both a local and a universal (English)
// name. MMD/VMD address bones and morphs by the *local* name, so the loader
// must keep that as the canonical name and stash the English one only for
// display. Storing the English name as canonical silently breaks motion
// binding for every bone whose English name differs from its local name.
void build_named_model(PmxBuilder& b) {
    const auto text = [&](const std::string& s) { b.text_utf8(s); };

    b.raw("PMX ");
    b.f32(2.0f);
    b.u8(8);
    b.u8(1);  // UTF-8
    b.u8(0);
    for (int i = 0; i < 6; ++i) {
        b.u8(1);  // every index size = 1 byte
    }
    text("NamedModel");
    text("");
    text("");
    text("");

    b.i32(0);  // vertices
    b.i32(0);  // surfaces
    b.i32(0);  // textures
    b.i32(0);  // materials

    // --- Bones: one root with distinct local/universal names ---
    b.i32(1);
    text("右足");
    text("right leg");
    b.vec3f(vec3(0.0f, 1.0f, 0.0f));
    b.i8(-1);
    b.i32(0);
    b.u8(0x00);
    b.u8(0x00);  // no flags
    b.vec3f(vec3(0.0f, 0.0f, 0.0f));

    // --- Morphs: one vertex morph with distinct local/universal names ---
    b.i32(1);
    text("まばたき");
    text("Blink");
    b.u8(1);   // panel
    b.u8(1);   // type: vertex
    b.i32(0);  // no offsets (this model has no vertices)

    b.i32(0);  // display frames
    b.i32(0);  // rigid bodies
    b.i32(0);  // joints
}

void test_local_names_are_canonical() {
    PmxBuilder b;
    build_named_model(b);
    const PmxResult result = parse_pmx(b.data(), b.size());
    MS_CHECK(result.ok);
    if (!result.ok) {
        return;
    }

    const Skeleton& skeleton = result.model.skeleton;
    MS_CHECK(skeleton.size() == 1);
    const Bone& bone = skeleton.bone(0);
    // Canonical name is the local one, so a VMD track named 右足 binds.
    MS_CHECK(bone.name == "右足");
    MS_CHECK(bone.name_en == "right leg");
    MS_CHECK(bone.display_name() == "right leg");
    MS_CHECK(skeleton.find_bone("右足") == 0);
    // The English name must not be the match key.
    MS_CHECK(skeleton.find_bone("right leg") == -1);

    const MorphSet& morphs = result.model.morphs;
    MS_CHECK(morphs.size() == 1);
    const MorphTarget& morph = morphs.target(0);
    MS_CHECK(morph.name == "まばたき");
    MS_CHECK(morph.name_en == "Blink");
    MS_CHECK(morph.display_name() == "Blink");
    MS_CHECK(morphs.find("まばたき") == 0);
    MS_CHECK(morphs.find("Blink") == -1);
}

void test_ik_roles_and_solve() {
    PmxBuilder b;
    build_ik_model(b);
    const PmxResult result = parse_pmx(b.data(), b.size());
    MS_CHECK(result.ok);
    if (!result.ok) {
        return;
    }

    const Skeleton& skeleton = result.model.skeleton;
    MS_CHECK(skeleton.ik_solvers().size() == 1);
    if (skeleton.ik_solvers().empty()) {
        return;
    }
    const IkSolver& solver = skeleton.ik_solvers()[0];

    // The chain end (PMX target index) is the effector; the flag-carrier is the
    // controller. Swapping these curls the chain away from its controller.
    const int ankle = skeleton.find_bone("ankle");
    const int ikctl = skeleton.find_bone("ikctl");
    MS_CHECK(solver.effector_bone == ankle);
    MS_CHECK(solver.goal_bone == ikctl);
    MS_CHECK(skeleton.bone(ikctl).ik_solver == 0);

    // Solving must pull the chain end onto the controller.
    Pose pose;
    pose.resize(skeleton.size());
    evaluate_pose_full(skeleton, pose, {}, true);
    const vec3 effector(pose.global[static_cast<std::size_t>(ankle)][3]);
    const vec3 goal(pose.global[static_cast<std::size_t>(ikctl)][3]);
    MS_CHECK_MSG(glm::length(effector - goal) < 0.05f,
                 "IK chain end did not reach its controller");

    // With IK off the chain end stays at its rest position, far away.
    Pose fk;
    fk.resize(skeleton.size());
    evaluate_pose_full(skeleton, fk, {}, false);
    MS_CHECK(glm::length(vec3(fk.global[static_cast<std::size_t>(ankle)][3]) -
                         goal) > 0.2f);
}

void test_header_and_vertices() {
    PmxBuilder b;
    build_model(b, true);
    const PmxResult result = parse_pmx(b.data(), b.size());
    MS_CHECK(result.ok);
    MS_CHECK_MSG(result.error.empty(), result.error);

    const Model& model = result.model;
    // Universal name is present, so it wins over the local one.
    MS_CHECK(model.name == "Test Model");
    MS_CHECK(model.mesh.vertices.size() == 5);
    MS_CHECK(model.mesh.indices.size() == 6);

    // Position and UV survive the round trip; Z is mirrored to right-handed.
    const Vertex& v0 = model.mesh.vertices[0];
    MS_CHECK_NEAR(v0.position, vec3(1.0f, 1.0f, 0.0f));
    MS_CHECK_NEAR(v0.uv, vec2(0.0f, 0.0f));
    MS_CHECK_NEAR(v0.normal, vec3(0.0f, 0.0f, -1.0f));

    // V3 sits at z=+3 in PMX, so it must arrive at z=-3.
    const Vertex& v3 = model.mesh.vertices[3];
    MS_CHECK_NEAR(v3.position, vec3(1.0f, 0.0f, -3.0f));
    MS_CHECK_NEAR(v3.uv, vec2(1.0f, 1.0f));

    // The Z-mirror flips each triangle's orientation, so the loader swaps the
    // last two vertices of every triangle to keep front faces counter-clockwise
    // (what the renderer culls against). A stored triangle (a, b, c) therefore
    // arrives as (a, c, b).
    MS_CHECK(model.mesh.indices[0] == 0u);
    MS_CHECK(model.mesh.indices[1] == 2u);
    MS_CHECK(model.mesh.indices[2] == 1u);
    MS_CHECK(model.mesh.indices[3] == 0u);
    MS_CHECK(model.mesh.indices[4] == 3u);
    MS_CHECK(model.mesh.indices[5] == 2u);
}

void test_weights() {
    PmxBuilder b;
    build_model(b, true);
    const PmxResult result = parse_pmx(b.data(), b.size());
    MS_CHECK(result.ok);
    if (!result.ok) {
        return;
    }
    const MeshData& mesh = result.model.mesh;

    // BDEF1: a single influence with full weight.
    MS_CHECK(mesh.vertices[0].bone_indices[0] == 1u);
    MS_CHECK_NEAR(mesh.vertices[0].bone_weights[0], 1.0f);
    MS_CHECK_NEAR(mesh.vertices[0].total_weight(), 1.0f);

    // BDEF2: the weight is a 4-byte float; the second is its complement.
    MS_CHECK(mesh.vertices[1].bone_indices[0] == 0u);
    MS_CHECK(mesh.vertices[1].bone_indices[1] == 1u);
    MS_CHECK_NEAR(mesh.vertices[1].bone_weights[0], 0.25f);
    MS_CHECK_NEAR(mesh.vertices[1].bone_weights[1], 0.75f);
    MS_CHECK_NEAR(mesh.vertices[1].total_weight(), 1.0f);

    // BDEF4: the nil slot (-1) must not produce an influence.
    MS_CHECK(mesh.vertices[2].bone_indices[0] == 0u);
    MS_CHECK(mesh.vertices[2].bone_indices[1] == 1u);
    MS_CHECK_NEAR(mesh.vertices[2].bone_weights[0], 0.5f);
    MS_CHECK_NEAR(mesh.vertices[2].bone_weights[1], 0.5f);
    MS_CHECK_NEAR(mesh.vertices[2].bone_weights[2], 0.0f);
    MS_CHECK_NEAR(mesh.vertices[2].bone_weights[3], 0.0f);
    MS_CHECK_NEAR(mesh.vertices[2].total_weight(), 1.0f);

    // SDEF: the two weights match BDEF2, and the trailing C/R0/R1 vec3s must
    // have been consumed without desynchronising the following vertex's data.
    MS_CHECK(mesh.vertices[4].bone_indices[0] == 0u);
    MS_CHECK(mesh.vertices[4].bone_indices[1] == 1u);
    MS_CHECK_NEAR(mesh.vertices[4].bone_weights[0], 0.5f);
    MS_CHECK_NEAR(mesh.vertices[4].bone_weights[1], 0.5f);
    MS_CHECK_NEAR(mesh.vertices[4].total_weight(), 1.0f);
}

void test_materials_and_textures() {
    PmxBuilder b;
    build_model(b, true);
    const PmxResult result = parse_pmx(b.data(), b.size());
    MS_CHECK(result.ok);
    if (!result.ok) {
        return;
    }

    MS_CHECK(result.model.texture_paths.size() == 1);
    MS_CHECK(result.model.texture_paths[0] == "tex/body.png");

    MS_CHECK(result.model.materials.size() == 1);
    const Material& m = result.model.materials[0];
    MS_CHECK(m.name == "Skin");
    MS_CHECK_NEAR(m.diffuse.x, 0.8f);
    MS_CHECK_NEAR(m.diffuse.y, 0.2f);
    MS_CHECK(m.texture == 0);
    MS_CHECK(m.double_sided);
    MS_CHECK(m.index_offset == 0u);
    MS_CHECK(m.index_count == 6u);
}

void test_bones_and_hierarchy() {
    PmxBuilder b;
    build_model(b, true);
    const PmxResult result = parse_pmx(b.data(), b.size());
    MS_CHECK(result.ok);
    if (!result.ok) {
        return;
    }

    const Skeleton& skeleton = result.model.skeleton;
    MS_CHECK(skeleton.size() == 2);

    // The root's local name is katakana with no universal fallback, so the
    // UTF-16LE bytes must have been decoded to UTF-8.
    MS_CHECK(skeleton.bone(0).name == "ルート");
    MS_CHECK(skeleton.bone(1).name == "child");

    MS_CHECK(skeleton.bone(0).parent == -1);
    MS_CHECK(skeleton.bone(1).parent == 0);

    MS_CHECK_NEAR(skeleton.bone(0).rest_position, vec3(0.0f, 0.0f, 0.0f));
    MS_CHECK_NEAR(skeleton.bone(1).rest_position, vec3(0.0f, 1.0f, 0.0f));

    // Rest globals compose down the chain, and inverse_bind undoes them.
    MS_CHECK_NEAR(skeleton.bone(1).rest_global[3], vec4(0.0f, 1.0f, 0.0f, 1.0f));
    MS_CHECK_NEAR(skeleton.bone(1).inverse_bind[3],
                  vec4(0.0f, -1.0f, 0.0f, 1.0f));
}

void test_skin_pose_math() {
    PmxBuilder b;
    build_model(b, true);
    const PmxResult result = parse_pmx(b.data(), b.size());
    MS_CHECK(result.ok);
    if (!result.ok) {
        return;
    }

    const Model& model = result.model;
    Pose pose;
    pose.resize(model.skeleton.size());
    evaluate_pose(model.skeleton, pose);

    // The rest pose must reproduce the source vertices exactly.
    const MeshData rest = skin_mesh(model.mesh, pose);
    MS_CHECK_NEAR(rest.vertices[0].position, vec3(1.0f, 1.0f, 0.0f));
    MS_CHECK_NEAR(rest.vertices[3].position, vec3(1.0f, 0.0f, -3.0f));

    // Rotate the child bone 90 degrees about Z. Its origin sits at (0,1,0),
    // so a vertex offset by +1 in X from that pivot must swing to +1 in Y.
    pose.delta[1].rotation = vec3(0.0f, 0.0f, 90.0f);
    evaluate_pose(model.skeleton, pose);
    const MeshData posed = skin_mesh(model.mesh, pose);

    MS_CHECK_NEAR(posed.vertices[0].position, vec3(0.0f, 2.0f, 0.0f));

    // V3 rides the root bone, which did not move, so it is untouched.
    MS_CHECK_NEAR(posed.vertices[3].position, vec3(1.0f, 0.0f, -3.0f));

    // V1 is 25% root / 75% child, so it lands between the two extremes.
    MS_CHECK_NEAR(posed.vertices[1].position, vec3(0.25f, 1.75f, 0.0f));

    // Rotating a bone is a rigid motion about the bone's own origin, so a
    // vertex fully weighted to that bone keeps its distance from the origin.
    // (Comparing two vertices that ride *different* bones would not be rigid.)
    const vec3 pivot(pose.global[1][3]);
    const float before = glm::length(model.mesh.vertices[0].position - pivot);
    const float after = glm::length(posed.vertices[0].position - pivot);
    MS_CHECK(ms::test::nearly(before, after, 1e-3f));
}

void test_morphs() {
    PmxBuilder b;
    build_model(b, true);
    const PmxResult result = parse_pmx(b.data(), b.size());
    MS_CHECK(result.ok);
    if (!result.ok) {
        return;
    }

    const MorphSet& morphs = result.model.morphs;
    MS_CHECK(morphs.size() == 2);
    MS_CHECK(morphs.find("Smile") == 0);
    MS_CHECK(morphs.find("Combo") == 1);
    MS_CHECK(morphs.find("nope") == -1);

    // Vertex morph: one offset, targeting vertex 4, moved +2 in Y. Z is
    // mirrored, but the offset has no Z component here.
    const MorphTarget& smile = morphs.target(0);
    MS_CHECK(smile.kind == MorphTarget::Kind::Vertex);
    MS_CHECK(smile.vertex_offsets.size() == 1);
    MS_CHECK(smile.vertex_offsets[0].vertex == 4u);
    MS_CHECK_NEAR(smile.vertex_offsets[0].offset, vec3(0.0f, 2.0f, 0.0f));

    // Group morph: references "Smile" at half strength.
    const MorphTarget& combo = morphs.target(1);
    MS_CHECK(combo.kind == MorphTarget::Kind::Group);
    MS_CHECK(combo.group_refs.size() == 1);
    MS_CHECK(combo.group_refs[0].first == 0u);
    MS_CHECK_NEAR(combo.group_refs[0].second, 0.5f);

    // Driving the group at full strength resolves to half a "Smile".
    MorphSet live = morphs;
    live.set_weight(1, 1.0f);
    const std::vector<float> resolved = live.resolved_weights();
    MS_CHECK(resolved.size() == 2);
    MS_CHECK_NEAR(resolved[0], 0.5f);

    // accumulate_offsets turns that into a per-vertex offset on vertex 4.
    std::vector<vec3> vertex_offsets;
    std::vector<BoneMorphOffset> bone_offsets;
    live.accumulate_offsets(result.model.mesh.vertices.size(),
                            result.model.skeleton.size(), vertex_offsets,
                            bone_offsets);
    MS_CHECK(vertex_offsets.size() == result.model.mesh.vertices.size());
    MS_CHECK_NEAR(vertex_offsets[4], vec3(0.0f, 1.0f, 0.0f));
    MS_CHECK(bone_offsets.empty());
}

void test_utf8_encoding() {
    PmxBuilder b;
    build_model(b, false);  // encoding = UTF-8
    const PmxResult result = parse_pmx(b.data(), b.size());
    MS_CHECK(result.ok);
    if (!result.ok) {
        return;
    }
    MS_CHECK(result.model.name == "Test Model");
    MS_CHECK(result.model.skeleton.bone(0).name == "ルート");
    MS_CHECK(result.model.materials[0].name == "Skin");
}

void test_error_handling() {
    // Bad signature.
    {
        PmxBuilder b;
        build_model(b, true);
        std::vector<std::uint8_t> bytes(b.data(), b.data() + b.size());
        bytes[0] = 'X';
        const PmxResult result = parse_pmx(bytes.data(), bytes.size());
        MS_CHECK(!result.ok);
        MS_CHECK(!result.error.empty());
    }
    // Truncated in the middle of the vertex section.
    {
        PmxBuilder b;
        build_model(b, true);
        const PmxResult result = parse_pmx(b.data(), b.size() / 2);
        MS_CHECK(!result.ok);
        MS_CHECK(!result.error.empty());
    }
    // Empty input.
    {
        const PmxResult result = parse_pmx(nullptr, 0);
        MS_CHECK(!result.ok);
    }
}

}  // namespace

int main() {
    test_header_and_vertices();
    test_weights();
    test_materials_and_textures();
    test_bones_and_hierarchy();
    test_local_names_are_canonical();
    test_ik_roles_and_solve();
    test_skin_pose_math();
    test_morphs();
    test_utf8_encoding();
    test_error_handling();
    return ms::test::summary("pmx");
}
