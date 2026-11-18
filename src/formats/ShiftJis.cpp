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

#include "formats/ShiftJis.hpp"

#include <cstdint>
#include <string>

#include "formats/SjisTable.hpp"

namespace ms {

namespace {

// Appends `cp` to `out` as UTF-8.
void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
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

// Binary search of the packed (key << 16 | codepoint) table.
std::uint32_t lookup_double_byte(std::uint16_t key) {
    std::uint32_t lo = 0;
    std::uint32_t hi = static_cast<std::uint32_t>(sjis::kDoubleByteCount);
    while (lo < hi) {
        const std::uint32_t mid = lo + (hi - lo) / 2;
        const std::uint32_t entry = sjis::kDoubleByte[mid];
        const std::uint16_t entry_key = static_cast<std::uint16_t>(entry >> 16);
        if (entry_key == key) {
            return entry & 0xFFFFu;
        }
        if (entry_key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return 0;
}

}  // namespace

std::string shift_jis_to_utf8(const std::uint8_t* bytes, std::size_t size) {
    std::string out;
    out.reserve(size);
    if (bytes == nullptr) {
        return out;
    }

    std::size_t i = 0;
    while (i < size) {
        const std::uint8_t lead = bytes[i];
        if (lead == 0x00) {
            break;  // fixed fields are NUL-padded
        }

        // ASCII and half-width katakana are single bytes with a table entry;
        // lead bytes of double-byte sequences have a zero entry.
        const std::uint16_t single = sjis::kSingleByte[lead];
        if (single != 0) {
            append_utf8(out, single);
            ++i;
            continue;
        }

        // A double-byte sequence: a trailing byte must follow.
        if (i + 1 >= size) {
            break;
        }
        const std::uint16_t key =
            static_cast<std::uint16_t>((lead << 8) | bytes[i + 1]);
        const std::uint32_t cp = lookup_double_byte(key);
        if (cp != 0) {
            append_utf8(out, cp);
        }
        i += 2;
    }
    return out;
}

}  // namespace ms
