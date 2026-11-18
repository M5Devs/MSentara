#!/usr/bin/env python3
"""Generate a tiny synthetic PMX 2.0 model for manual/headless testing.

The bytes here are produced from scratch (no copyrighted asset) and mirror the
layout the unit tests use, so `MSentara --pmx` can be exercised end to end.
"""
import struct
import sys


class B:
    def __init__(self):
        self.b = bytearray()

    def u8(self, v):
        self.b += struct.pack("<B", v & 0xFF)

    def i8(self, v):
        self.b += struct.pack("<b", v)

    def i32(self, v):
        self.b += struct.pack("<i", v)

    def u16(self, v):
        self.b += struct.pack("<H", v & 0xFFFF)

    def f32(self, v):
        self.b += struct.pack("<f", v)

    def v2(self, x, y):
        self.f32(x)
        self.f32(y)

    def v3(self, x, y, z):
        self.f32(x)
        self.f32(y)
        self.f32(z)

    def v4(self, x, y, z, w):
        self.f32(x)
        self.f32(y)
        self.f32(z)
        self.f32(w)

    def text(self, s):
        raw = s.encode("utf-16-le")
        self.i32(len(raw))
        self.b += raw


def build():
    b = B()
    b.b += b"PMX "
    b.f32(2.0)
    b.u8(8)
    b.u8(0)          # UTF-16LE
    b.u8(0)          # additional vec4
    for _ in range(6):
        b.u8(1)      # all index sizes = 1 byte
    b.text("テスト")   # local name (multi-byte on purpose)
    b.text("TestCube")
    b.text("")
    b.text("")

    # --- Vertices: a unit cube (8 verts, 12 tris), split 6/6 across two bones.
    verts = [
        (-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
        (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1),
    ]
    b.i32(len(verts))
    for i, (x, y, z) in enumerate(verts):
        b.v3(float(x), float(y), float(z))
        b.v3(0.0, 1.0, 0.0)
        b.v2(0.0, 0.0)
        if i < 4:
            # Front four: BDEF1, rigidly on bone 0.
            b.u8(0)      # BDEF1
            b.i8(0)      # bone 0
        else:
            # Back four: BDEF2, 50/50 across both bones. The weight is a
            # 4-byte IEEE-754 float (PMX), not a [0,100] byte (PMD).
            b.u8(1)      # BDEF2
            b.i8(0)      # bone 0
            b.i8(1)      # bone 1
            b.f32(0.5)   # bone 0 weight
        b.f32(1.0)       # edge scale

    # --- Surfaces: 12 triangles (36 indices).
    faces = [
        (0, 1, 2), (0, 2, 3),   # -Z
        (5, 4, 7), (5, 7, 6),   # +Z
        (4, 0, 3), (4, 3, 7),   # -X
        (1, 5, 6), (1, 6, 2),   # +X
        (4, 5, 1), (4, 1, 0),   # -Y
        (3, 2, 6), (3, 6, 7),   # +Y
    ]
    b.i32(len(faces) * 3)
    for f in faces:
        for idx in f:
            b.u8(idx)

    # --- Textures: none.
    b.i32(0)

    # --- Materials: one, covering every index.
    b.i32(1)
    b.text("Body")
    b.text("Body")
    b.v4(0.9, 0.5, 0.4, 1.0)   # diffuse
    b.v3(0.1, 0.1, 0.1)        # specular
    b.f32(5.0)
    b.v3(0.5, 0.5, 0.5)        # ambient
    b.u8(0x00)                 # flags: single sided
    b.v4(0.0, 0.0, 0.0, 1.0)   # edge colour
    b.f32(1.0)                 # edge scale
    b.i8(-1)                   # texture index (none)
    b.i8(-1)                   # environment index
    b.u8(0)                    # environment blend
    b.u8(1)                    # toon reference: internal (byte follows)
    b.u8(0)                    # toon byte
    b.text("")                 # metadata
    b.i32(len(faces) * 3)      # surface count

    # --- Bones: root + child (child at y=1), plus an IK handle the child
    # reaches for. The child carries the IK solver (flag 0x0020) with the root
    # as its one link, so solving moves the cube.
    b.i32(3)
    b.text("root")
    b.text("")
    b.v3(0.0, 0.0, 0.0)
    b.i8(-1)
    b.i32(0)
    b.u8(0x00)
    b.u8(0x00)
    b.v3(0.0, 1.0, 0.0)        # tail

    b.text("child")
    b.text("")
    b.v3(0.0, 1.0, 0.0)
    b.i8(0)
    b.i32(0)
    b.u8(0x20)                 # flags low: IK
    b.u8(0x00)
    b.v3(0.0, 2.0, 0.0)        # tail
    b.i8(2)                    # IK target bone index
    b.i32(20)                  # iteration count
    b.f32(0.0)                 # per-step limit angle (0 = none)
    b.i32(1)                   # link count
    b.i8(0)                    # link bone: root
    b.u8(0)                    # link has no angle limits

    b.text("ik_handle")
    b.text("")
    b.v3(0.6, 1.6, 0.0)        # a little off-axis, so the chain must bend
    b.i8(-1)
    b.i32(0)
    b.u8(0x00)
    b.u8(0x00)
    b.v3(0.6, 2.6, 0.0)        # tail

    # --- Morphs: a vertex morph and a group morph that drives it.
    b.i32(2)

    b.text("Smile")
    b.text("")
    b.u8(1)                    # panel: eyebrow
    b.u8(1)                    # type: vertex
    b.i32(1)                   # one offset
    b.u8(0)                    # vertex 0
    b.v3(0.0, 1.0, 0.0)        # offset

    b.text("Combo")
    b.text("")
    b.u8(1)                    # panel
    b.u8(0)                    # type: group
    b.i32(1)
    b.u8(0)                    # morph index 0
    b.f32(0.5)

    # --- Display frames: one frame with a bone entry, so the parser has a
    # non-empty section to walk before the rigid bodies.
    b.i32(1)
    b.text("Root")
    b.text("")
    b.u8(0)                    # not a special frame
    b.i32(1)                   # one entry
    b.u8(0)                    # bone frame
    b.i8(0)                    # bone index 0

    # --- Rigid bodies: a kinematic body on the root and a dynamic body on the
    # child, joined below. The dynamic body hangs off the kinematic one, so the
    # smoke run exercises the solver end to end.
    b.i32(2)
    # Kinematic anchor on the root bone.
    b.text("root_body")
    b.text("")
    b.i8(0)                    # bone 0
    b.u8(0)                    # group 0
    b.u16(0)                   # collision mask
    b.u8(0)                    # shape: sphere
    b.v3(0.1, 0.0, 0.0)        # radius
    b.v3(0.0, 0.0, 0.0)        # position
    b.v3(0.0, 0.0, 0.0)        # rotation
    b.f32(0.0)                 # mass
    b.f32(0.5)                 # move attenuation
    b.f32(0.5)                 # rotation damping
    b.f32(0.0)                 # repulsion
    b.f32(0.5)                 # friction
    b.u8(0)                    # mode: kinematic
    # Dynamic body on the child bone.
    b.text("child_body")
    b.text("")
    b.i8(1)                    # bone 1
    b.u8(1)                    # group 1
    b.u16(0)                   # collision mask
    b.u8(2)                    # shape: capsule
    b.v3(0.1, 0.3, 0.0)        # radius, half length
    b.v3(0.0, 1.0, 0.0)        # position
    b.v3(0.0, 0.0, 0.0)        # rotation
    b.f32(1.0)                 # mass
    b.f32(0.5)                 # move attenuation
    b.f32(0.5)                 # rotation damping
    b.f32(0.1)                 # repulsion
    b.f32(0.4)                 # friction
    b.u8(1)                    # mode: dynamic

    # --- Joints: one spring joint hanging the dynamic body off the kinematic.
    b.i32(1)
    b.text("child_joint")
    b.text("")
    b.u8(0)                    # type: spring 6DOF
    b.i8(0)                    # body A
    b.i8(1)                    # body B
    b.v3(0.0, 0.5, 0.0)        # position
    b.v3(0.0, 0.0, 0.0)        # rotation
    b.v3(-0.05, -0.05, -0.05)  # translation min
    b.v3(0.05, 0.05, 0.05)     # translation max
    b.v3(-0.5, -0.5, -0.5)     # rotation min
    b.v3(0.5, 0.5, 0.5)        # rotation max
    b.v3(1.0, 1.0, 1.0)        # translation spring
    b.v3(1.0, 1.0, 1.0)        # rotation spring
    return bytes(b.b)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "test_model.pmx"
    with open(path, "wb") as f:
        f.write(build())
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
