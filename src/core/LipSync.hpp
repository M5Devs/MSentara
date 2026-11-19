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
#include <vector>

#include "core/Animation.hpp"
#include "core/AudioAnalysis.hpp"

namespace ms {

// How an audio envelope is turned into mouth motion. Shared by the 3D path
// (a morph weight) and the 2D path (a sprite-variant index).
struct LipSyncSettings {
    // 3D: the morph to drive. Its weight becomes the mouth-open amount.
    std::string morph_name;

    // Multiplies the envelope before it is used, so a quiet recording can still
    // open the mouth fully. Clamped to [0, 1] afterwards.
    float gain = 1.0f;

    // Envelope levels below this are treated as silence, so background noise
    // does not make the mouth flap continuously.
    float threshold = 0.05f;

    // Frame range to generate. A negative end means "as far as the envelope
    // reaches". The start is where the audio begins on the timeline.
    int frame_start = 0;
    int frame_end = -1;

    // 2D: the variant index shown for a closed mouth, and the ascending list of
    // open-mouth variants to pick from as the level rises. With N open
    // variants, level 1 selects the last and level just above the threshold the
    // first.
    int closed_variant = 0;
    std::vector<int> open_variants;

    // 2D: below this level the mouth is drawn closed. Usually a little above
    // `threshold`, so the mouth does not flicker between the two states.
    float close_threshold = 0.12f;
};

// Builds a stepped morph track that opens the mouth with the audio. Each frame
// carries one key (the weight is continuous, so linear interpolation between
// them is harmless). Returns an empty track when the envelope is empty or no
// morph name is set.
ScalarTrack generate_morph_lipsync(const AudioEnvelope& envelope,
                                   const LipSyncSettings& settings);

// Builds a stepped variant track for a 2D mouth. Each frame picks the closed
// variant, or one of `open_variants` chosen by the level. Returns an empty
// track when the envelope is empty or there are no open variants.
VariantTrack generate_variant_lipsync(const AudioEnvelope& envelope,
                                      const LipSyncSettings& settings);

// Parses a comma- or space-separated list of whole numbers, ignoring blanks and
// non-numeric tokens. Shared by the editor's "Open Variants" field and the
// --lipsync-2d command-line flag.
std::vector<int> parse_variant_indices(const std::string& text);

}  // namespace ms
