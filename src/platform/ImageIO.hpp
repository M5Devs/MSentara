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

}  // namespace ms
