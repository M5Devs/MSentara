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

#include "ui/Scene2DInstance.hpp"

#include "platform/ImageIO.hpp"

namespace ms {

namespace {

// Decodes a layer image (unflipped: the 2D quad is already in the file's row
// order) and uploads it. Returns the pixel size alongside the texture handle
// so the caller can set the layer's `size`.
struct LoadedImage {
    GpuTexture texture;
    vec2 size{0.0f, 0.0f};
    bool ok = false;
};

LoadedImage load_layer_image(const std::string& path, Renderer& renderer) {
    LoadedImage loaded;
    if (path.empty()) {
        return loaded;
    }
    const TextureData decoded = load_texture_file_2d(path);
    if (!decoded.valid()) {
        return loaded;
    }
    loaded.texture = renderer.create_texture(decoded);
    loaded.size = vec2(static_cast<float>(decoded.width),
                       static_cast<float>(decoded.height));
    loaded.ok = loaded.texture.valid();
    return loaded;
}

}  // namespace

LayerId Scene2DInstance::add_group(const std::string& name, LayerId parent) {
    const LayerId id = scene_.add_layer(name, parent);
    // Keep the texture vector index-aligned with the layer vector; a group has
    // no image, so its slot holds an invalid handle.
    textures_.push_back(GpuTexture{});
    return id;
}

LayerId Scene2DInstance::add_layer_from_image(const std::string& name,
                                              const std::string& path,
                                              Renderer& renderer,
                                              LayerId parent) {
    const LayerId id = add_group(name, parent);
    Layer2D* layer = scene_.find(id);
    layer->image_path = path;

    const LoadedImage loaded = load_layer_image(path, renderer);
    if (loaded.ok) {
        textures_[static_cast<std::size_t>(id)] = loaded.texture;
        layer->size = loaded.size;
        // Default the pivot to the image centre: the natural rotation point
        // for a cutout, and a sensible starting point the user can move.
        layer->pivot = loaded.size * 0.5f;
    }
    return id;
}

bool Scene2DInstance::set_layer_image(LayerId id, const std::string& path,
                                      Renderer& renderer) {
    Layer2D* layer = scene_.find(id);
    if (layer == nullptr) {
        return false;
    }
    const LoadedImage loaded = load_layer_image(path, renderer);
    if (!loaded.ok) {
        return false;
    }
    renderer.destroy_texture(textures_[static_cast<std::size_t>(id)]);
    textures_[static_cast<std::size_t>(id)] = loaded.texture;
    layer->image_path = path;
    layer->size = loaded.size;
    return true;
}

mat4 Scene2DInstance::layer_model_matrix(LayerId id) const {
    const Layer2D* layer = scene_.find(id);
    if (layer == nullptr) {
        return mat4(1.0f);
    }
    // The shared quad is unit-sized, so scale it to the image's pixel size
    // before applying the layer's world transform.
    return scene_.world_matrix(id) * glm::scale(mat4(1.0f), vec3(layer->size, 1.0f));
}

void Scene2DInstance::draw(Renderer& renderer, const GpuMesh& quad,
                           const mat4& view_proj) const {
    for (const LayerId id : scene_.draw_order()) {
        const Layer2D* layer = scene_.find(id);
        if (layer == nullptr || !layer->visible || layer->opacity <= 0.0f) {
            continue;
        }
        // A group node (no size) has nothing to draw.
        if (layer->size.x <= 0.0f || layer->size.y <= 0.0f) {
            continue;
        }
        const GpuTexture& texture = textures_[static_cast<std::size_t>(id)];
        // Layer opacity multiplies the tint's alpha; the tint's RGB lets a
        // layer be coloured without touching its image.
        const vec4 tint(layer->tint.r, layer->tint.g, layer->tint.b,
                        layer->tint.a * layer->opacity);
        renderer.draw_sprite(quad, texture, layer_model_matrix(id), view_proj,
                             tint);
    }
}

void Scene2DInstance::clear(Renderer& renderer) {
    for (GpuTexture& texture : textures_) {
        renderer.destroy_texture(texture);
    }
    textures_.clear();
    scene_.clear();
    clip_ = AnimationClip{};
}

}  // namespace ms
