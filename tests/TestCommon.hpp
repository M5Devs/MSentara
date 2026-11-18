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

// Minimal self-contained assertion helpers so the core tests need no external
// test framework. Each test binary returns the number of failed checks.

#include <cmath>
#include <cstdio>
#include <string>

#include "core/Math.hpp"

namespace ms::test {

inline int g_failures = 0;
inline int g_checks = 0;

inline void report(bool ok, const char* expr, const char* file, int line,
                   const std::string& detail = {}) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s:%d: %s%s%s\n", file, line, expr,
                     detail.empty() ? "" : " — ", detail.c_str());
    }
}

inline int summary(const char* name) {
    std::printf("[%s] %d checks, %d failures\n", name, g_checks, g_failures);
    return g_failures;
}

inline bool nearly(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

inline bool nearly(const vec2& a, const vec2& b, float eps = 1e-4f) {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps);
}

inline bool nearly(const vec3& a, const vec3& b, float eps = 1e-4f) {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) &&
           nearly(a.z, b.z, eps);
}

inline bool nearly(const vec4& a, const vec4& b, float eps = 1e-4f) {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) &&
           nearly(a.z, b.z, eps) && nearly(a.w, b.w, eps);
}

inline bool nearly(const mat4& a, const mat4& b, float eps = 1e-4f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!nearly(a[c][r], b[c][r], eps)) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace ms::test

#define MS_CHECK(expr) ::ms::test::report((expr), #expr, __FILE__, __LINE__)
#define MS_CHECK_MSG(expr, msg) \
    ::ms::test::report((expr), #expr, __FILE__, __LINE__, (msg))
#define MS_CHECK_NEAR(a, b) \
    ::ms::test::report(::ms::test::nearly((a), (b)), #a " ~= " #b, __FILE__, __LINE__)
