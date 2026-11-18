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
    // Keep the texture vectors index-aligned with the layer vector; a group has
    // no image, so its slots stay empty.
    textures_.push_back(GpuTexture{});
    variant_textures_.emplace_back();
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
    ensure_texture_slots();
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

bool Scene2DInstance::add_layer_variant(LayerId id, const std::string& name,
                                        const std::string& path,
                                        Renderer& renderer) {
    ensure_texture_slots();
    Layer2D* layer = scene_.find(id);
    if (layer == nullptr || path.empty()) {
        return false;
    }
    const LoadedImage loaded = load_layer_image(path, renderer);
    if (!loaded.ok) {
        return false;
    }
    layer->variants.push_back(LayerVariant{name, path, loaded.size});
    variant_textures_[static_cast<std::size_t>(id)].push_back(loaded.texture);
    // A variant just added becomes the active one so the user sees it, and the
    // quad resizes to the swapped-in image.
    layer->active_variant = static_cast<int>(layer->variants.size()) - 1;
    layer->size = loaded.size;
    return true;
}

void Scene2DInstance::ensure_texture_slots() {
    // Layers can also be appended straight to the core scene (a preset
    // instantiation), bypassing add_group; grow the texture vectors so every
    // layer id has a slot and reload/upload stays in bounds.
    const std::size_t count = scene_.size();
    if (textures_.size() < count) {
        textures_.resize(count);
    }
    if (variant_textures_.size() < count) {
        variant_textures_.resize(count);
    }
}

void Scene2DInstance::reload_layer_textures(LayerId id, Renderer& renderer) {
    ensure_texture_slots();
    Layer2D* layer = scene_.find(id);
    if (layer == nullptr || id < 0 ||
        static_cast<std::size_t>(id) >= textures_.size()) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(id);

    renderer.destroy_texture(textures_[index]);
    textures_[index] = GpuTexture{};
    LoadedImage base;
    if (!layer->image_path.empty()) {
        base = load_layer_image(layer->image_path, renderer);
    }
    if (base.ok) {
        textures_[index] = base.texture;
    }

    for (GpuTexture& texture : variant_textures_[index]) {
        renderer.destroy_texture(texture);
    }
    variant_textures_[index].clear();
    for (LayerVariant& variant : layer->variants) {
        const LoadedImage loaded = load_layer_image(variant.image_path, renderer);
        variant_textures_[index].push_back(loaded.texture);
        if (loaded.ok) {
            variant.size = loaded.size;
        }
    }

    // The quad follows whichever image is active (the variant, if any). The
    // base and variant sizes were captured above, so no third decode is needed.
    if (!layer->variants.empty() && layer->active_variant >= 0 &&
        static_cast<std::size_t>(layer->active_variant) < layer->variants.size()) {
        layer->size = layer->variants[static_cast<std::size_t>(
            layer->active_variant)].size;
    } else if (base.ok) {
        layer->size = base.size;
    }
}

const GpuTexture& Scene2DInstance::texture_for(LayerId id,
                                               int variant_index) const {
    static const GpuTexture invalid;
    if (id < 0 || static_cast<std::size_t>(id) >= textures_.size()) {
        return invalid;
    }
    const std::size_t index = static_cast<std::size_t>(id);
    if (variant_index >= 0 &&
        static_cast<std::size_t>(variant_index) <
            variant_textures_[index].size()) {
        return variant_textures_[index][static_cast<std::size_t>(variant_index)];
    }
    return textures_[index];
}

const GpuTexture& Scene2DInstance::active_texture(LayerId id) const {
    const Layer2D* layer = scene_.find(id);
    if (layer == nullptr) {
        static const GpuTexture invalid;
        return invalid;
    }
    return texture_for(id, layer->active_variant);
}

mat4 Scene2DInstance::layer_model_matrix(LayerId id, const Skeleton* skeleton,
                                         const Pose* pose) const {
    const Layer2D* layer = scene_.find(id);
    if (layer == nullptr) {
        return mat4(1.0f);
    }
    // The shared quad is unit-sized, so scale it to the image's pixel size
    // before applying the layer's world transform. active_size() follows the
    // active variant, so a swapped expression resizes the quad too.
    return scene_.world_matrix(id, skeleton, pose) *
           glm::scale(mat4(1.0f), vec3(layer->active_size(), 1.0f));
}

void Scene2DInstance::draw(Renderer& renderer, const GpuMesh& quad,
                           const mat4& view_proj, const Skeleton* skeleton,
                           const Pose* pose) const {
    for (const LayerId id : scene_.draw_order()) {
        const Layer2D* layer = scene_.find(id);
        if (layer == nullptr || !layer->visible || layer->opacity <= 0.0f) {
            continue;
        }
        // A group node (no size) has nothing to draw.
        if (layer->active_size().x <= 0.0f || layer->active_size().y <= 0.0f) {
            continue;
        }
        const GpuTexture& texture = active_texture(id);
        // Layer opacity multiplies the tint's alpha; the tint's RGB lets a
        // layer be coloured without touching its image.
        const vec4 tint(layer->tint.r, layer->tint.g, layer->tint.b,
                        layer->tint.a * layer->opacity);
        renderer.draw_sprite(quad, texture,
                             layer_model_matrix(id, skeleton, pose), view_proj,
                             tint);
    }
}

void Scene2DInstance::draw_ghost_frame(Renderer& renderer, const GpuMesh& quad,
                                       const mat4& view_proj,
                                       const Scene2D& ghost_scene,
                                       const vec4& tint,
                                       const Skeleton* skeleton,
                                       const Pose* pose) const {
    for (const LayerId id : ghost_scene.draw_order()) {
        const Layer2D* layer = ghost_scene.find(id);
        if (layer == nullptr || !layer->visible) {
            continue;
        }
        // A ghost is drawn even at zero layer opacity (the whole point is to
        // show a neighbouring frame), but only when there is geometry to draw.
        if (layer->active_size().x <= 0.0f || layer->active_size().y <= 0.0f) {
            continue;
        }
        // Sample the *ghost* scene's variant (the copy evaluated at that frame),
        // not the live layer's, so a variant-swap animation ghosts correctly.
        const GpuTexture& texture = texture_for(id, layer->active_variant);
        renderer.draw_sprite(quad, texture,
                             ghost_scene.world_matrix(id, skeleton, pose) *
                                 glm::scale(mat4(1.0f),
                                            vec3(layer->active_size(), 1.0f)),
                             view_proj, tint);
    }
}

void Scene2DInstance::clear(Renderer& renderer) {
    for (GpuTexture& texture : textures_) {
        renderer.destroy_texture(texture);
    }
    textures_.clear();
    for (std::vector<GpuTexture>& variants : variant_textures_) {
        for (GpuTexture& texture : variants) {
            renderer.destroy_texture(texture);
        }
    }
    variant_textures_.clear();
    scene_.clear();
    clip_ = AnimationClip{};
}

}  // namespace ms
