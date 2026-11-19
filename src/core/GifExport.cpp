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

#include "core/GifExport.hpp"

#include <cmath>
#include <cstddef>

namespace ms {

int gif_delay_centiseconds(float fps) {
    if (!(fps > 0.0f) || !std::isfinite(fps)) {
        fps = 30.0f;
    }
    // 100 cs per second, one frame every 1/fps seconds.
    const double delay = 100.0 / static_cast<double>(fps);
    int centiseconds = static_cast<int>(std::lround(delay));
    if (centiseconds < 1) {
        centiseconds = 1;
    } else if (centiseconds > 65535) {
        centiseconds = 65535;
    }
    return centiseconds;
}

bool has_gif_extension(const std::string& path) {
    const std::size_t sep = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep) ||
        dot + 1 >= path.size()) {
        return false;
    }
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext == "gif";
}

}  // namespace ms
