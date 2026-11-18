#!/usr/bin/env bash
# MSentara — container build helper
# Copyright (C) 2026 The MSentara Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs the toolchain and system dependencies this container does not ship
# with, then configures, builds and tests the project. Everything outside
# /workspace is lost when the sandbox restarts, so this is safe to re-run.
#
# Usage:  scripts/build.sh [Release|Debug] [--no-test]

set -u

BUILD_TYPE="${1:-Release}"
RUN_TESTS=1
for arg in "$@"; do
    [ "$arg" = "--no-test" ] && RUN_TESTS=0
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build"

# --- Toolchain ------------------------------------------------------------
export PATH="$HOME/.local/bin:$PATH"
if ! command -v cmake >/dev/null 2>&1; then
    echo "== installing cmake + ninja =="
    python3 -m pip install --user -q cmake ninja
fi

# --- System libraries -----------------------------------------------------
# sokol_app's X11 backend needs the X11/GL headers and unversioned .so
# symlinks, which only the -dev packages provide.
if [ ! -e /usr/include/X11/Xlib.h ] || [ ! -e /usr/lib/x86_64-linux-gnu/libGL.so ]; then
    echo "== installing system dev packages =="
    sudo -n apt-get update -qq
    sudo -n apt-get install -y -qq \
        libgl1-mesa-dev libglu1-mesa-dev libglx-dev \
        libx11-dev libxcursor-dev libxi-dev libxext-dev \
        libxrandr-dev libxinerama-dev \
        xvfb x11-utils imagemagick mesa-utils
fi

# --- Configure + build ----------------------------------------------------
# -j 2: the sandbox has 4 cores but a parallel sokol/glm build has tripped the
# runtime before, so keep the peak memory modest.
echo "== configuring ($BUILD_TYPE) =="
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" || exit 1

echo "== building =="
cmake --build "$BUILD_DIR" -j 2 || exit 1

# --- Test -----------------------------------------------------------------
if [ "$RUN_TESTS" = "1" ]; then
    echo "== ctest =="
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
