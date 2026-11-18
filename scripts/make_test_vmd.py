#!/usr/bin/env python3
# MSentara — cross-platform 2D/3D animation studio
# Copyright (C) 2026 The MSentara Authors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
"""Writes a small synthetic VMD motion for the headless smoke test.

The motion animates the two bones the synthetic PMX model (made by
scripts/make_test_pmx.py) exposes, so `--pmx model.pmx --vmd motion.vmd`
produces visible movement without shipping any copyrighted MMD asset.

Usage:  python3 scripts/make_test_vmd.py /tmp/test_motion.vmd
"""

import struct
import sys


def f32(v):
    return struct.pack("<f", v)


def u32(v):
    return struct.pack("<I", v)


def fixed(text, size):
    raw = text.encode("cp932")
    return raw + b"\x00" * (size - len(raw))


def interpolation(x0, y0, x1, y1):
    """The 64-byte block: four copies of a 16-byte control pattern.

    Byte (control * 4 + channel) holds that control value, where control is
    0=x0, 1=y0, 2=x1, 3=y1 and channel is 0=X, 1=Y, 2=Z, 3=rotation. This is
    the layout saba's VMDFile.cpp / VMDAnimation.cpp read.
    """
    one = bytearray(16)
    for channel in range(4):
        one[0 * 4 + channel] = x0
        one[1 * 4 + channel] = y0
        one[2 * 4 + channel] = x1
        one[3 * 4 + channel] = y1
    return bytes(one) * 4


def bone_key(name, frame, translate, quaternion, bezier=(0, 0, 127, 127)):
    data = fixed(name, 15)
    data += u32(frame)
    data += f32(translate[0]) + f32(translate[1]) + f32(translate[2])
    data += f32(quaternion[0]) + f32(quaternion[1])
    data += f32(quaternion[2]) + f32(quaternion[3])
    data += interpolation(*bezier)
    return data


def quat_axis_angle(axis, degrees):
    """Quaternion (x, y, z, w) for a rotation about `axis` by `degrees`."""
    import math
    half = math.radians(degrees) * 0.5
    s = math.sin(half)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def morph_key(name, frame, weight):
    """A VMD morph keyframe: a 15-byte name, a frame, and a weight."""
    data = fixed(name, 15)
    data += u32(frame)
    data += f32(weight)
    return data


def build():
    # Header: "Vocaloid Motion Data 0002" is 25 bytes in a 30-byte field,
    # followed by a 20-byte model name.
    out = fixed("Vocaloid Motion Data 0002", 30)
    out += fixed("Test Model", 20)

    keys = []
    # Bone names must match the synthetic PMX model (scripts/make_test_pmx.py
    # exposes "root" and "child"), so the headless smoke test animates it.
    # "root": a vertical bob over 30 frames with an ease curve.
    keys.append(bone_key("root", 0, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0),
                         bezier=(0, 127, 127, 0)))
    keys.append(bone_key("root", 15, (0.0, 2.0, 0.0), (0.0, 0.0, 0.0, 1.0)))
    keys.append(bone_key("root", 30, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)))
    # "child": a 90-degree swing about Z.
    keys.append(bone_key("child", 0, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)))
    keys.append(bone_key("child", 30, (0.0, 0.0, 0.0),
                         quat_axis_angle((0.0, 0.0, 1.0), 90.0)))

    out += u32(len(keys))
    for k in keys:
        out += k

    # Morph keyframes: drive the synthetic "Combo" morph from 0 to 1 and back,
    # so the headless run exercises the scalar-track -> morph-weight path.
    morphs = []
    morphs.append(morph_key("Combo", 0, 0.0))
    morphs.append(morph_key("Combo", 15, 1.0))
    morphs.append(morph_key("Combo", 30, 0.0))
    out += u32(len(morphs))
    for m in morphs:
        out += m
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/test_motion.vmd"
    data = build()
    with open(path, "wb") as f:
        f.write(data)
    print("wrote %s (%d bytes)" % (path, len(data)))


if __name__ == "__main__":
    main()
