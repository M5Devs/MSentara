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

#include "TestCommon.hpp"

#include "core/Timeline.hpp"

using namespace ms;
using namespace ms::test;

namespace {

void test_empty_curve_returns_fallback() {
    Curve c;
    MS_CHECK(c.empty());
    MS_CHECK(nearly(c.sample(10.0f, 3.5f), 3.5f));
}

void test_keys_stay_sorted() {
    Curve c;
    c.set_key(20, 2.0f);
    c.set_key(5, 0.5f);
    c.set_key(12, 1.2f);
    MS_CHECK(c.keys.size() == 3);
    MS_CHECK(c.keys[0].frame == 5);
    MS_CHECK(c.keys[1].frame == 12);
    MS_CHECK(c.keys[2].frame == 20);
}

void test_set_key_replaces() {
    Curve c;
    c.set_key(10, 1.0f);
    c.set_key(10, 9.0f);
    MS_CHECK(c.keys.size() == 1);
    MS_CHECK(nearly(c.keys[0].value, 9.0f));
}

void test_remove_key() {
    Curve c;
    c.set_key(1, 1.0f);
    c.set_key(2, 2.0f);
    MS_CHECK(c.remove_key(1));
    MS_CHECK(c.keys.size() == 1);
    MS_CHECK(c.keys[0].frame == 2);
    MS_CHECK(!c.remove_key(99));
}

void test_linear_interpolation() {
    Curve c;
    c.interpolation = Interpolation::Linear;
    c.set_key(0, 0.0f);
    c.set_key(10, 10.0f);
    MS_CHECK(nearly(c.sample(5.0f), 5.0f));
    MS_CHECK(nearly(c.sample(2.5f), 2.5f));
}

void test_constant_interpolation() {
    Curve c;
    c.interpolation = Interpolation::Constant;
    c.set_key(0, 1.0f);
    c.set_key(10, 9.0f);
    // Holds the left key's value across the whole segment.
    MS_CHECK(nearly(c.sample(9.9f), 1.0f));
    MS_CHECK(nearly(c.sample(10.0f), 9.0f));
}

void test_bezier_is_monotonic_and_eased() {
    Curve c;
    c.interpolation = Interpolation::Bezier;
    c.set_key(0, 0.0f);
    c.set_key(10, 10.0f);
    // Smooth-step midpoint is exactly halfway, but the ends ease in/out.
    MS_CHECK(nearly(c.sample(5.0f), 5.0f, 1e-3f));
    MS_CHECK(c.sample(1.0f) < 1.0f);   // slower than linear at the start
    MS_CHECK(c.sample(9.0f) > 9.0f);   // slower than linear at the end
}

void test_clamps_outside_range() {
    Curve c;
    c.set_key(5, 2.0f);
    c.set_key(15, 8.0f);
    MS_CHECK(nearly(c.sample(-100.0f), 2.0f));
    MS_CHECK(nearly(c.sample(1000.0f), 8.0f));
}

void test_action_curve_lookup() {
    Action a;
    MS_CHECK(!a.has_channel(Channel::PositionX));
    a.curve(Channel::PositionX).set_key(0, 1.0f);
    MS_CHECK(a.has_channel(Channel::PositionX));
    MS_CHECK(a.find_curve(Channel::PositionY) == nullptr);
    // Requesting the same channel twice must not duplicate it.
    a.curve(Channel::PositionX).set_key(5, 2.0f);
    MS_CHECK(a.curves.size() == 1);
    MS_CHECK(a.curve(Channel::PositionX).keys.size() == 2);
}

void test_action_frame_range() {
    Action a;
    a.curve(Channel::PositionX).set_key(3, 0.0f);
    a.curve(Channel::PositionX).set_key(30, 1.0f);
    a.curve(Channel::PositionY).set_key(10, 0.0f);
    MS_CHECK(a.frame_range_start() == 3);
    MS_CHECK(a.frame_range_end() == 30);
}

void test_action_empty_range_is_zero() {
    Action a;
    MS_CHECK(a.frame_range_start() == 0);
    MS_CHECK(a.frame_range_end() == 0);
}

void test_channel_names() {
    MS_CHECK(std::string(channel_name(Channel::PositionX)) == "Position X");
    MS_CHECK(std::string(channel_name(Channel::ScaleZ)) == "Scale Z");
}

void test_timeline_frame_clamp() {
    Timeline t;
    t.frame_start = 1;
    t.frame_end = 100;
    t.set_frame(-50.0f);
    MS_CHECK(nearly(t.current_frame, 1.0f));
    t.set_frame(5000.0f);
    MS_CHECK(nearly(t.current_frame, 100.0f));
}

void test_timeline_step() {
    Timeline t;
    t.frame_start = 1;
    t.frame_end = 100;
    t.set_frame(10.0f);
    t.step(5);
    MS_CHECK(nearly(t.current_frame, 15.0f));
    t.step(-100);
    MS_CHECK(nearly(t.current_frame, 1.0f));
}

void test_timeline_advance_only_when_playing() {
    Timeline t;
    t.fps = 24.0f;
    t.set_frame(1.0f);
    t.playing = false;
    MS_CHECK(nearly(t.advance(1.0f), 1.0f));

    t.playing = true;
    // One second at 24 fps advances 24 frames.
    MS_CHECK(nearly(t.advance(1.0f), 25.0f));
}

void test_timeline_advance_wraps() {
    Timeline t;
    t.fps = 10.0f;
    t.frame_start = 0;
    t.frame_end = 10;
    t.set_frame(9.0f);
    t.playing = true;
    const float f = t.advance(1.0f);  // would land on 19, wraps into range
    MS_CHECK(f >= 0.0f && f <= 10.0f);
}

void test_duration_seconds() {
    Timeline t;
    t.frame_start = 1;
    t.frame_end = 49;
    t.fps = 24.0f;
    MS_CHECK(nearly(t.duration_seconds(), 2.0f));
    t.fps = 0.0f;
    MS_CHECK(nearly(t.duration_seconds(), 0.0f));
}

}  // namespace

int main() {
    test_empty_curve_returns_fallback();
    test_keys_stay_sorted();
    test_set_key_replaces();
    test_remove_key();
    test_linear_interpolation();
    test_constant_interpolation();
    test_bezier_is_monotonic_and_eased();
    test_clamps_outside_range();
    test_action_curve_lookup();
    test_action_frame_range();
    test_action_empty_range_is_zero();
    test_channel_names();
    test_timeline_frame_clamp();
    test_timeline_step();
    test_timeline_advance_only_when_playing();
    test_timeline_advance_wraps();
    test_duration_seconds();
    return ms::test::summary("timeline");
}
