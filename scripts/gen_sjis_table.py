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
"""Regenerates src/formats/SjisTable.hpp from Python's cp932 codec.

VMD stores bone names as fixed 15-byte Shift-JIS (CP932) fields. Decoding
them needs a Shift-JIS table, and pulling in ICU or iconv would break the
Web/Android builds, so the table is generated once here and checked in.

Usage:  python3 scripts/gen_sjis_table.py
"""

import os

OUT = os.path.join(os.path.dirname(__file__), "..", "src", "formats",
                   "SjisTable.hpp")

# Lead bytes of CP932 double-byte sequences.
LEADS = list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD))
# Trail bytes.
TRAILS = list(range(0x40, 0x7F)) + list(range(0x80, 0xFD))

HEADER = """// MSentara — cross-platform 2D/3D animation studio
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
//
// GENERATED FILE — do not edit by hand.
// Regenerate with: python3 scripts/gen_sjis_table.py
//
// Shift-JIS (CP932) to Unicode tables, extracted from Python's cp932 codec
// so the parser carries no ICU/iconv dependency and works on every target.

#pragma once

#include <cstdint>

namespace ms::sjis {

// Unicode code point for each single byte, or 0 when the byte starts a
// double-byte sequence (or is undefined).
inline constexpr std::uint16_t kSingleByte[256] = {
"""


def single_byte_table():
    rows = []
    for base in range(0, 256, 8):
        cells = []
        for b in range(base, base + 8):
            try:
                decoded = bytes([b]).decode("cp932")
                cp = ord(decoded) if len(decoded) == 1 else 0
            except Exception:
                cp = 0
            cells.append("0x%04X" % cp)
        rows.append("    " + ", ".join(cells) + ",")
    return "\n".join(rows)


def double_byte_entries():
    entries = []
    for lead in LEADS:
        for trail in TRAILS:
            try:
                decoded = bytes([lead, trail]).decode("cp932")
            except Exception:
                continue
            if len(decoded) != 1:
                continue
            cp = ord(decoded)
            if cp > 0xFFFF:
                continue
            key = (lead << 8) | trail
            entries.append((key << 16) | cp)
    entries.sort()
    return entries


def double_byte_table(entries):
    rows = []
    for i in range(0, len(entries), 8):
        cells = ["0x%08Xu" % v for v in entries[i:i + 8]]
        rows.append("    " + ", ".join(cells) + ",")
    return "\n".join(rows)


def main():
    entries = double_byte_entries()
    parts = [HEADER]
    parts.append(single_byte_table())
    parts.append("};\n\n")
    parts.append("// Sorted packed entries for double-byte sequences, keyed by\n")
    parts.append("// (lead << 8 | trail) << 16 | code_point. Binary search this.\n")
    parts.append("inline constexpr std::uint32_t kDoubleByte[] = {\n")
    parts.append(double_byte_table(entries))
    parts.append("\n};\n\n")
    parts.append("inline constexpr int kDoubleByteCount = %d;\n\n" % len(entries))
    parts.append("}  // namespace ms::sjis\n")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("".join(parts))
    print("wrote %s (%d double-byte entries)" % (OUT, len(entries)))


if __name__ == "__main__":
    main()
