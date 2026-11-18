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

#include "core/Morph.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace ms {

int MorphSet::add(MorphTarget target) {
    targets_.push_back(std::move(target));
    weights_.push_back(0.0f);
    return static_cast<int>(targets_.size()) - 1;
}

int MorphSet::find(const std::string& name) const {
    for (std::size_t i = 0; i < targets_.size(); ++i) {
        if (targets_[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MorphSet::reset_weights() {
    std::fill(weights_.begin(), weights_.end(), 0.0f);
}

void MorphSet::set_weight(int index, float weight) {
    if (index < 0 || index >= static_cast<int>(weights_.size())) {
        return;
    }
    weights_[static_cast<std::size_t>(index)] = weight;
}

std::vector<float> MorphSet::resolved_weights() const {
    // Start from the directly driven weights, then add each group's members.
    // Group morphs may reference other groups; the recursion resolves them
    // depth-first and a visited set breaks any cycle a malformed file creates.
    std::vector<float> resolved = weights_;
    const std::size_t count = targets_.size();

    std::vector<char> visiting(count, 0);
    std::function<void(std::size_t, float)> expand = [&](std::size_t index,
                                                         float weight) {
        if (index >= count || visiting[index] != 0) {
            return;
        }
        const MorphTarget& target = targets_[index];
        if (target.kind != MorphTarget::Kind::Group) {
            return;
        }
        visiting[index] = 1;
        for (const auto& ref : target.group_refs) {
            const std::size_t child = ref.first;
            if (child >= count) {
                continue;
            }
            const float contribution = weight * ref.second;
            if (targets_[child].kind == MorphTarget::Kind::Group) {
                expand(child, contribution);
            } else {
                resolved[child] += contribution;
            }
        }
        visiting[index] = 0;
    };

    for (std::size_t i = 0; i < count; ++i) {
        if (targets_[i].kind == MorphTarget::Kind::Group) {
            expand(i, weights_[i]);
        }
    }
    return resolved;
}

void MorphSet::accumulate_offsets(std::size_t vertex_count,
                                  std::size_t bone_count,
                                  std::vector<vec3>& vertex_offsets,
                                  std::vector<BoneMorphOffset>& bone_offsets) const {
    vertex_offsets.assign(vertex_count, vec3(0.0f));
    bone_offsets.clear();

    const std::vector<float> resolved = resolved_weights();
    for (std::size_t i = 0; i < targets_.size() && i < resolved.size(); ++i) {
        const float weight = resolved[i];
        if (weight == 0.0f) {
            continue;
        }
        const MorphTarget& target = targets_[i];
        switch (target.kind) {
            case MorphTarget::Kind::Vertex:
                for (const VertexMorphOffset& offset : target.vertex_offsets) {
                    if (offset.vertex < vertex_count) {
                        vertex_offsets[offset.vertex] += offset.offset * weight;
                    }
                }
                break;
            case MorphTarget::Kind::Bone:
                for (const BoneMorphOffset& offset : target.bone_offsets) {
                    if (offset.bone < bone_count) {
                        BoneMorphOffset accumulated = offset;
                        accumulated.translation *= weight;
                        // Scale the rotation by the weight via a shortest-arc
                        // slerp from the identity, which is what a bone morph's
                        // weight means.
                        accumulated.rotation = glm::slerp(
                            quat(1.0f, 0.0f, 0.0f, 0.0f), offset.rotation,
                            weight);
                        bone_offsets.push_back(accumulated);
                    }
                }
                break;
            case MorphTarget::Kind::Group:
                break;  // already expanded into the leaf morphs
        }
    }
}

}  // namespace ms
