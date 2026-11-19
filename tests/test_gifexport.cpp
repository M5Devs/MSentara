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

// Tests for the animated-GIF export: the pure delay/path maths in core, and a
// real multi-frame encode through the platform GifWriter. The writer needs no
// GPU or window (it only touches the filesystem), so this links the platform
// library the same way test_audio does.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "TestCommon.hpp"

#include "core/GifExport.hpp"
#include "platform/ImageIO.hpp"

using namespace ms;
using namespace ms::test;

namespace {

// Reads a whole file into a byte vector. Returns false when it cannot be
// opened, which the caller reports as a failure.
bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return true;
}

bool starts_with(const std::vector<std::uint8_t>& bytes, const char* text) {
    const std::string prefix(text);
    if (bytes.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (bytes[i] != static_cast<std::uint8_t>(prefix[i])) {
            return false;
        }
    }
    return true;
}

void test_delay_centiseconds() {
    // 100 cs per second: 30 fps -> 3.33 cs -> 3.
    MS_CHECK(gif_delay_centiseconds(30.0f) == 3);
    // 25 fps -> 4 cs exactly.
    MS_CHECK(gif_delay_centiseconds(25.0f) == 4);
    // 10 fps -> 10 cs.
    MS_CHECK(gif_delay_centiseconds(10.0f) == 10);
    // 100 fps -> 1 cs (rounds to 1, never 0: a 0 delay means "use the viewer
    // default", which would play far too slowly).
    MS_CHECK(gif_delay_centiseconds(100.0f) == 1);
    // A non-positive or non-finite fps falls back to 30 fps.
    MS_CHECK(gif_delay_centiseconds(0.0f) == 3);
    MS_CHECK(gif_delay_centiseconds(-5.0f) == 3);
}

void test_gif_extension() {
    MS_CHECK(has_gif_extension("out.gif"));
    MS_CHECK(has_gif_extension("OUT.GIF"));
    MS_CHECK(has_gif_extension("dir/sub/clip.Gif"));
    MS_CHECK(!has_gif_extension("out.png"));
    MS_CHECK(!has_gif_extension("noext"));
    MS_CHECK(!has_gif_extension(""));
    // A dot in a directory name is not the file's extension.
    MS_CHECK(!has_gif_extension("v1.2/out"));
}

// Encodes a small multi-frame animation and checks the bytes on disk are a
// valid animated GIF: the "GIF89a" signature, a well-formed screen descriptor
// matching the requested size, more than one image, and the 0x3B trailer.
void test_writes_valid_animated_gif() {
    const std::string path = "test_gif_out.gif";
    std::remove(path.c_str());

    constexpr int kWidth = 16;
    constexpr int kHeight = 12;
    constexpr int kFrames = 3;
    const int delay = gif_delay_centiseconds(30.0f);

    GifWriter writer;
    MS_CHECK(writer.begin(path, kWidth, kHeight, delay));
    MS_CHECK(writer.is_open());

    std::vector<std::uint8_t> frame(
        static_cast<std::size_t>(kWidth) * kHeight * 4u, 0u);
    for (int f = 0; f < kFrames; ++f) {
        // A different solid colour per frame, so the encoder cannot collapse
        // the sequence into a single unchanged image.
        for (std::size_t i = 0; i < frame.size(); i += 4) {
            frame[i + 0] = static_cast<std::uint8_t>(40 + f * 70);
            frame[i + 1] = static_cast<std::uint8_t>(200 - f * 60);
            frame[i + 2] = static_cast<std::uint8_t>(30 + f * 20);
            frame[i + 3] = 255;
        }
        MS_CHECK(writer.append(frame.data(), kWidth, kHeight));
    }
    MS_CHECK(writer.frames_written() == kFrames);
    MS_CHECK(writer.finish());
    MS_CHECK(!writer.is_open());

    std::vector<std::uint8_t> bytes;
    MS_CHECK_MSG(read_file(path, bytes), path);
    // "GIF89a" is the animated-GIF signature gif.h always writes.
    MS_CHECK(starts_with(bytes, "GIF89a"));
    // The screen descriptor follows the 6-byte signature: width then height,
    // both little-endian 16-bit.
    if (bytes.size() >= 10) {
        const int width = bytes[6] | (bytes[7] << 8);
        const int height = bytes[8] | (bytes[9] << 8);
        MS_CHECK(width == kWidth);
        MS_CHECK(height == kHeight);
    }
    // Every image starts with the 0x2C separator; a valid multi-frame encode
    // therefore contains at least one, and a real animation at least two
    // (gif.h writes one image descriptor per frame).
    int image_separators = 0;
    for (std::uint8_t b : bytes) {
        if (b == 0x2C) {
            ++image_separators;
        }
    }
    MS_CHECK_MSG(image_separators >= 2, "expected >= 2 image descriptors");
    // The trailer byte closes the file.
    MS_CHECK(!bytes.empty() && bytes.back() == 0x3B);
    // And the file is not a degenerate empty container.
    MS_CHECK(bytes.size() > 100);

    std::remove(path.c_str());
}

// A size mismatch between frames must be rejected rather than corrupt the
// stream (the header fixed the dimensions).
void test_rejects_size_mismatch() {
    const std::string path = "test_gif_mismatch.gif";
    std::remove(path.c_str());

    GifWriter writer;
    MS_CHECK(writer.begin(path, 8, 8, 4));
    std::vector<std::uint8_t> frame(8u * 8u * 4u, 255u);
    MS_CHECK(writer.append(frame.data(), 8, 8));
    // Wrong dimensions: refused.
    MS_CHECK(!writer.append(frame.data(), 4, 8));
    MS_CHECK(!writer.append(nullptr, 8, 8));
    MS_CHECK(writer.frames_written() == 1);
    writer.finish();
    std::remove(path.c_str());
}

void test_begin_rejects_bad_input() {
    GifWriter writer;
    MS_CHECK(!writer.begin("", 16, 16, 4));
    MS_CHECK(!writer.begin("test_gif_bad.gif", 0, 16, 4));
    MS_CHECK(!writer.append(nullptr, 16, 16));
    // finish() on a writer that never opened is a no-op, not a crash.
    MS_CHECK(!writer.finish());
    std::remove("test_gif_bad.gif");
}

}  // namespace

int main() {
    test_delay_centiseconds();
    test_gif_extension();
    test_writes_valid_animated_gif();
    test_rejects_size_mismatch();
    test_begin_rejects_bad_input();
    return summary("gifexport");
}
