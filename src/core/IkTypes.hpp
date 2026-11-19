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

#include <vector>

#include "core/Math.hpp"

namespace ms {

// One link of a CCD-IK chain: a bone the solver is allowed to rotate to bring
// the chain end (effector) onto the controller (goal). `enable_limit` clamps
// the link's local Euler angles to [limit_min, limit_max] (radians), which is
// how a knee is stopped from bending backwards.
struct IkLink {
    int bone = -1;
    bool enable_limit = false;
    vec3 limit_min{0.0f, 0.0f, 0.0f};  // radians
    vec3 limit_max{0.0f, 0.0f, 0.0f};  // radians
};

// A CCD-IK chain: the bone the solver moves (the effector, the chain end), the
// bone it must bring that effector onto (the goal, the IK controller / handle),
// the iteration budget, a per-step angle clamp, and the links the solver may
// rotate.
//
// PMX stores the controller as the bone carrying the IK flag and the chain end
// as the solver's "target index", and MMD moves the chain end toward the
// controller. Naming the two the other way round silently inverts the solve:
// a hair chain then flies *away* from its handle and curls up into a U instead
// of hanging. So the chain end is `effector_bone` and the controller is
// `goal_bone`.
//
// Kept format-neutral: a hand-built rig or a future 2D chain fills the same
// structure.
struct IkSolver {
    int goal_bone = -1;      // IK controller / handle (PMX flag carrier)
    int effector_bone = -1;  // chain end that moves (PMX "target index")
    int iterations = 1;
    // Per-step clamp on how far a link may rotate in one iteration (radians).
    // PMX stores a "limit radian"; a non-positive value means "no clamp".
    float limit_angle = 0.0f;
    std::vector<IkLink> links;
    bool enabled = true;
};

}  // namespace ms
