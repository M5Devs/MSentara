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

#include "platform/ImageIO.hpp"

#include <cstddef>
#include <cstdint>

// stb_image is a single-header library; exactly one translation unit may
// define the implementation, and this is it.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// stb_image_write shares no state with stb_image, so its implementation can
// live in the same translation unit without a conflict.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace ms {

TextureData load_texture_file(const std::string& path) {
    TextureData texture;

    // OpenGL's texture origin is bottom-left while every common image format
    // stores rows top-down, so flip on load. PMX UVs then line up without a
    // shader-side tweak.
    stbi_set_flip_vertically_on_load(1);

    int width = 0;
    int height = 0;
    int channels = 0;
    // Requesting 4 channels makes stb expand greyscale, RGB and palette
    // sources to RGBA8, so the caller never has to care about the source
    // format.
    stbi_uc* pixels =
        stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return texture;
    }

    texture.width = width;
    texture.height = height;
    texture.channels = 4;
    const std::size_t byte_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    texture.pixels.assign(pixels, pixels + byte_count);

    stbi_image_free(pixels);
    return texture;
}

TextureData load_texture_file_2d(const std::string& path) {
    // stbi's flip flag is global state, so clear it explicitly rather than
    // relying on the default: a prior load_texture_file call leaves it set.
    stbi_set_flip_vertically_on_load(0);

    TextureData texture;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return texture;
    }

    texture.width = width;
    texture.height = height;
    texture.channels = 4;
    const std::size_t byte_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    texture.pixels.assign(pixels, pixels + byte_count);

    stbi_image_free(pixels);
    return texture;
}

TextureData make_white_texture() {
    TextureData texture;
    texture.width = 1;
    texture.height = 1;
    texture.channels = 4;
    texture.pixels = {255u, 255u, 255u, 255u};
    return texture;
}

std::string resolve_texture_path(const std::string& model_dir,
                                 const std::string& texture_path) {
    if (texture_path.empty()) {
        return {};
    }

    // PMX authors on Windows often write backslashes; normalise so the same
    // model loads on every platform.
    std::string relative = texture_path;
    for (char& c : relative) {
        if (c == '\\') {
            c = '/';
        }
    }

    // An absolute reference (rare, and usually a broken export) is used as-is.
    const bool absolute = relative.front() == '/' ||
                          (relative.size() > 1 && relative[1] == ':');
    if (absolute || model_dir.empty()) {
        return relative;
    }
    if (model_dir.back() == '/') {
        return model_dir + relative;
    }
    return model_dir + "/" + relative;
}

bool write_png_rgba(const std::string& path, int width, int height,
                    const std::uint8_t* pixels) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    // stride 0 means "tightly packed", which is what the caller guarantees.
    // stb writes rows top-down, matching the buffer orientation we pass in.
    return stbi_write_png(path.c_str(), width, height, 4, pixels, 0) != 0;
}

}  // namespace ms
