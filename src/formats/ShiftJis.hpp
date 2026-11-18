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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ms {

// Decodes Shift-JIS (CP932) text into UTF-8.
//
// VMD bone names are fixed-width Shift-JIS fields, so reading them needs a
// Shift-JIS table. Rather than depend on ICU or iconv — neither of which is
// available on Web/Android — this uses the compact table in SjisTable.hpp
// (generated from the CP932 codec by scripts/gen_sjis_table.py).
//
// `size` bytes are decoded; a NUL byte ends the text early (the fixed fields
// are NUL-padded). Bytes that do not form a valid CP932 sequence are dropped.
std::string shift_jis_to_utf8(const std::uint8_t* bytes, std::size_t size);

// Convenience overload for a fixed NUL-padded C field.
inline std::string shift_jis_to_utf8(const char* field, std::size_t size) {
    return shift_jis_to_utf8(reinterpret_cast<const std::uint8_t*>(field),
                             size);
}

}  // namespace ms
