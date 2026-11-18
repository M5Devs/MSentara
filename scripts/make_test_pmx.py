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
        b.u8(0)          # BDEF1
        b.i8(0 if i < 4 else 1)   # front four -> bone 0, back four -> bone 1
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

    # --- Bones: root + child (child at y=1), so posing is visible.
    b.i32(2)
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
    b.u8(0x00)
    b.u8(0x00)
    b.v3(0.0, 2.0, 0.0)        # tail
    return bytes(b.b)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "test_model.pmx"
    with open(path, "wb") as f:
        f.write(build())
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
