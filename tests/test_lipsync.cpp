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

// Tests for the lip-sync generators. Both are pure core code: an envelope in,
// a track out, so they run with no audio device or GPU.

#include <vector>

#include "TestCommon.hpp"

#include "core/AudioAnalysis.hpp"
#include "core/LipSync.hpp"

using namespace ms;
using namespace ms::test;

namespace {

// A hand-built envelope: `frames` buckets, with the values given. Skips the
// RMS analysis so the expected track values are exact.
AudioEnvelope envelope_from(const std::vector<float>& levels, double fps) {
    AudioEnvelope env;
    env.levels = levels;
    env.frame_count = static_cast<int>(levels.size());
    env.frames_per_second = fps;
    return env;
}

void test_morph_track_shape() {
    // Four frames: silent, quiet, loud, silent.
    const AudioEnvelope env =
        envelope_from({0.0f, 0.2f, 1.0f, 0.0f}, 30.0);

    LipSyncSettings settings;
    settings.morph_name = "mouth_open";
    settings.threshold = 0.05f;
    settings.gain = 1.0f;

    const ScalarTrack track = generate_morph_lipsync(env, settings);
    MS_CHECK(track.name == "mouth_open");
    MS_CHECK(track.keys.size() == 4u);

    // Silent frames stay shut.
    MS_CHECK(nearly(track.keys[0].value, 0.0f));
    MS_CHECK(nearly(track.keys[3].value, 0.0f));
    // The loudest frame opens fully.
    MS_CHECK(nearly(track.keys[2].value, 1.0f));
    // A mid level opens partway: > 0 and < 1.
    MS_CHECK(track.keys[1].value > 0.0f);
    MS_CHECK(track.keys[1].value < 1.0f);
    // Frames are one apart from the start.
    MS_CHECK(track.keys[0].frame == 0);
    MS_CHECK(track.keys[3].frame == 3);
}

void test_morph_threshold_silences_noise() {
    // A level below the threshold must produce an exactly-zero weight.
    const AudioEnvelope env = envelope_from({0.04f, 0.06f}, 30.0);
    LipSyncSettings settings;
    settings.morph_name = "mouth";
    settings.threshold = 0.05f;

    const ScalarTrack track = generate_morph_lipsync(env, settings);
    MS_CHECK(track.keys.size() == 2u);
    MS_CHECK(track.keys[0].value == 0.0f);  // 0.04 < 0.05
    MS_CHECK(track.keys[1].value > 0.0f);   // 0.06 > 0.05
}

void test_morph_gain_clamps() {
    // A gain that would push past 1 is clamped, so a weight never exceeds the
    // morph's full-open value.
    const AudioEnvelope env = envelope_from({0.6f}, 30.0);
    LipSyncSettings settings;
    settings.morph_name = "mouth";
    settings.threshold = 0.0f;
    settings.gain = 4.0f;

    const ScalarTrack track = generate_morph_lipsync(env, settings);
    MS_CHECK(track.keys.size() == 1u);
    MS_CHECK(nearly(track.keys[0].value, 1.0f));
}

void test_morph_frame_range() {
    const AudioEnvelope env = envelope_from({0.5f, 0.5f, 0.5f, 0.5f, 0.5f},
                                            30.0);
    LipSyncSettings settings;
    settings.morph_name = "mouth";
    settings.frame_start = 1;
    settings.frame_end = 3;

    const ScalarTrack track = generate_morph_lipsync(env, settings);
    MS_CHECK(track.keys.size() == 3u);
    MS_CHECK(track.keys.front().frame == 1);
    MS_CHECK(track.keys.back().frame == 3);
}

void test_morph_empty_inputs() {
    // No morph name -> empty track.
    LipSyncSettings settings;
    const AudioEnvelope env = envelope_from({1.0f}, 30.0);
    MS_CHECK(generate_morph_lipsync(env, settings).empty());

    // No envelope -> empty track.
    settings.morph_name = "mouth";
    MS_CHECK(generate_morph_lipsync(AudioEnvelope{}, settings).empty());
}

void test_variant_track_mapping() {
    // Silent, quiet, loud, silent. Three open variants (indices 1,2,3) and a
    // closed variant 0.
    const AudioEnvelope env =
        envelope_from({0.0f, 0.3f, 1.0f, 0.0f}, 30.0);
    LipSyncSettings settings;
    settings.closed_variant = 0;
    settings.open_variants = {1, 2, 3};
    settings.close_threshold = 0.12f;

    const VariantTrack track = generate_variant_lipsync(env, settings);
    MS_CHECK(track.keys.size() == 4u);
    // Silence -> closed.
    MS_CHECK(track.keys[0].value == 0);
    MS_CHECK(track.keys[3].value == 0);
    // Loud -> the last open variant.
    MS_CHECK(track.keys[2].value == 3);
    // A quiet open frame -> an open variant, not closed.
    MS_CHECK(track.keys[1].value != 0);
    MS_CHECK(track.keys[1].value >= 1 && track.keys[1].value <= 3);
}

void test_variant_steps_not_blends() {
    // The sampled value between two keys is the earlier key's value: the track
    // is stepped, so a variant index never blends.
    const AudioEnvelope env =
        envelope_from({1.0f, 0.0f, 1.0f}, 30.0);
    LipSyncSettings settings;
    settings.closed_variant = 0;
    settings.open_variants = {1};
    settings.close_threshold = 0.5f;

    const VariantTrack track = generate_variant_lipsync(env, settings);
    // At frame 0.9 (between key 0 = open and key 1 = closed) the step holds
    // the open value.
    MS_CHECK(track.sample(0.9f, -1) == 1);
    // At frame 1.9 it holds the closed value.
    MS_CHECK(track.sample(1.9f, -1) == 0);
}

void test_variant_empty_inputs() {
    const AudioEnvelope env = envelope_from({1.0f}, 30.0);
    LipSyncSettings settings;
    // No open variants -> empty.
    MS_CHECK(generate_variant_lipsync(env, settings).empty());
    // No envelope -> empty.
    settings.open_variants = {1};
    MS_CHECK(generate_variant_lipsync(AudioEnvelope{}, settings).empty());
}

void test_variant_close_threshold_holds_shut() {
    // A level between the morph threshold and the close threshold keeps the 2D
    // mouth shut, so light noise does not make it flicker.
    const AudioEnvelope env = envelope_from({0.08f, 0.2f}, 30.0);
    LipSyncSettings settings;
    settings.closed_variant = 0;
    settings.open_variants = {1};
    settings.close_threshold = 0.15f;

    const VariantTrack track = generate_variant_lipsync(env, settings);
    MS_CHECK(track.keys[0].value == 0);  // 0.08 < 0.15
    MS_CHECK(track.keys[1].value == 1);  // 0.2 >= 0.15
}

void test_parse_variant_indices() {
    MS_CHECK(parse_variant_indices("1,2,3") == (std::vector<int>{1, 2, 3}));
    MS_CHECK(parse_variant_indices(" 4 , 5 ") == (std::vector<int>{4, 5}));
    MS_CHECK(parse_variant_indices("") == (std::vector<int>{}));
    // Junk between separators is skipped, not fatal.
    MS_CHECK(parse_variant_indices("7,x,9") == (std::vector<int>{7, 9}));
    // No separators at all still yields one number.
    MS_CHECK(parse_variant_indices("12") == (std::vector<int>{12}));
}

}  // namespace

int main() {
    test_morph_track_shape();
    test_morph_threshold_silences_noise();
    test_morph_gain_clamps();
    test_morph_frame_range();
    test_morph_empty_inputs();
    test_variant_track_mapping();
    test_variant_steps_not_blends();
    test_variant_empty_inputs();
    test_variant_close_threshold_holds_shut();
    test_parse_variant_indices();
    return summary("lipsync");
}
