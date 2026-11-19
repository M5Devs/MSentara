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

#include "core/LipSync.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ms {
namespace {

// The inclusive frame range a generator should emit for this envelope.
void frame_range(const AudioEnvelope& envelope, const LipSyncSettings& settings,
                 int& first, int& last) {
    first = std::max(0, settings.frame_start);
    const int available = envelope.frame_count > 0
                              ? envelope.frame_count - 1
                              : 0;
    last = (settings.frame_end < 0) ? available
                                    : std::min(settings.frame_end, available);
    if (last < first) {
        last = first - 1;  // empty range
    }
}

// Envelope level for a frame, gained, thresholded and clamped to [0, 1].
float level_for(const AudioEnvelope& envelope, const LipSyncSettings& settings,
                int frame) {
    const float raw = envelope.at(frame) * settings.gain;
    if (raw < settings.threshold) {
        return 0.0f;
    }
    // Rescale the survivors so a level just above the threshold maps near 0
    // and the loudest bucket maps to 1, rather than jumping straight up.
    const float span = std::max(1e-4f, 1.0f - settings.threshold);
    return std::min(1.0f, (raw - settings.threshold) / span);
}

}  // namespace

ScalarTrack generate_morph_lipsync(const AudioEnvelope& envelope,
                                   const LipSyncSettings& settings) {
    ScalarTrack track;
    track.name = settings.morph_name;
    if (envelope.empty() || settings.morph_name.empty()) {
        return track;
    }

    int first = 0;
    int last = -1;
    frame_range(envelope, settings, first, last);
    for (int f = first; f <= last; ++f) {
        ScalarKey key;
        key.frame = f;
        key.value = level_for(envelope, settings, f);
        track.keys.push_back(key);
    }
    return track;
}

VariantTrack generate_variant_lipsync(const AudioEnvelope& envelope,
                                      const LipSyncSettings& settings) {
    VariantTrack track;
    if (envelope.empty() || settings.open_variants.empty()) {
        return track;
    }

    int first = 0;
    int last = -1;
    frame_range(envelope, settings, first, last);

    const int open_count = static_cast<int>(settings.open_variants.size());
    for (int f = first; f <= last; ++f) {
        const float raw = envelope.at(f) * settings.gain;
        int variant = settings.closed_variant;
        // Below the close threshold the mouth stays shut. This is deliberately
        // separate from `threshold` (used by the 3D morph) so the 2D mouth can
        // have its own hysteresis-free cutoff.
        if (raw >= settings.close_threshold) {
            const float span = std::max(1e-4f, 1.0f - settings.close_threshold);
            const float level = std::min(1.0f, (raw - settings.close_threshold) / span);
            // Map the level across the open variants: a quiet syllable picks
            // the first, the loudest picks the last.
            const int idx = std::min(
                open_count - 1,
                static_cast<int>(std::floor(level * static_cast<float>(open_count))));
            variant = settings.open_variants[static_cast<std::size_t>(
                std::max(0, idx))];
        }
        VariantKey key;
        key.frame = f;
        key.value = variant;
        track.keys.push_back(key);
    }
    return track;
}

std::vector<int> parse_variant_indices(const std::string& text) {
    std::vector<int> out;
    const char* s = text.c_str();
    while (*s != '\0') {
        while (*s == ',' || *s == ' ' || *s == '\t') {
            ++s;
        }
        if (*s == '\0') {
            break;
        }
        char* end = nullptr;
        const long value = std::strtol(s, &end, 10);
        if (end == s) {
            while (*s != '\0' && *s != ',' && *s != ' ' && *s != '\t') {
                ++s;
            }
            continue;
        }
        out.push_back(static_cast<int>(value));
        s = end;
    }
    return out;
}

}  // namespace ms
