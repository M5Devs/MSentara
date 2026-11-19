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

#include <string>

#include "core/Material.hpp"

namespace ms {

// Image decoding lives in the platform layer because it is an implementation
// detail of the graphics backend: core only ever describes pixels.
//
// Every source format (PNG, JPG, TGA, BMP, ...) is normalised to tightly
// packed RGBA8, which is what Renderer::create_texture expects.

// Decodes an image file. Returns an invalid TextureData (rather than
// throwing) when the file is missing or undecodable, so callers can fall back
// to a placeholder.
TextureData load_texture_file(const std::string& path);

// Decodes an image file *without* the vertical flip load_texture_file applies.
// 2D layers use a pixel-space, y-down quad whose UVs already match the file's
// row order, so flipping here would draw every sprite upside down.
TextureData load_texture_file_2d(const std::string& path);

// A 1x1 opaque white texture. Untextured materials sample this so the shader
// can multiply by the texture unconditionally, with no branch.
TextureData make_white_texture();

// Joins a directory and a (possibly backslash-separated) relative path, which
// is how PMX stores its texture references.
std::string resolve_texture_path(const std::string& model_dir,
                                 const std::string& texture_path);

// Writes tightly packed RGBA8 pixels to a PNG file. `pixels` must hold
// width * height * 4 bytes, with the first row being the *top* row (the same
// orientation a decoded image has). Returns false if the file cannot be
// written. Used by the offline frame-sequence exporter.
bool write_png_rgba(const std::string& path, int width, int height,
                    const std::uint8_t* pixels);

// An animated-GIF writer that encodes frames one at a time, so a long export
// never holds the whole sequence in memory (a 1080p frame is ~8 MB RGBA and a
// GIF export can be hundreds of frames). The caller renders a frame, calls
// append(), and calls finish() once at the end.
//
// The GIF encoder ignores alpha (it has no alpha channel), so an RGBA buffer is
// expected and its alpha byte is dropped. `delay_cs` is the per-frame delay in
// centiseconds; see core/GifExport.hpp for how it is derived from fps.
class GifWriter {
public:
    GifWriter() = default;
    ~GifWriter();
    GifWriter(const GifWriter&) = delete;
    GifWriter& operator=(const GifWriter&) = delete;

    // Opens `path` and writes the GIF header. The frame size is fixed for the
    // whole animation; every append() must supply the same width/height. Returns
    // false if the file cannot be created.
    bool begin(const std::string& path, int width, int height, int delay_cs);

    // Appends one RGBA8 frame (width * height * 4 bytes, top row first). The
    // first append() fixes the frame size. Returns false on a size mismatch or
    // a write error.
    bool append(const std::uint8_t* rgba, int width, int height);

    // Writes the trailer and closes the file. Safe to call more than once; the
    // destructor calls it if it was not already.
    bool finish();

    bool is_open() const { return open_; }
    int frames_written() const { return frames_written_; }

private:
    // gif.h's GifWriter is a C struct; holding it by value here would expose
    // that header to every includer. A pointer keeps ImageIO.hpp free of it.
    struct Impl;
    Impl* impl_ = nullptr;
    bool open_ = false;
    int width_ = 0;
    int height_ = 0;
    int frames_written_ = 0;
};

}  // namespace ms
