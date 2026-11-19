# MSentara — cross-platform 2D/3D animation studio
# Copyright (C) 2026 The MSentara Authors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.20)

project(MSentara
    VERSION 0.1.0
    DESCRIPTION "Cross-platform 2D/3D animation studio"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MSentaraDependencies)
include(MSentaraWarnings)

# Detect Emscripten. The web build produces a single .html/.js/.wasm bundle and
# uses the GLES3 (WebGL2) sokol backend.
if(EMSCRIPTEN)
    set(MSENTARA_WEB TRUE)
    # WebGL2 cannot do the multi-sampled offscreen resolve sokol uses for the
    # default framebuffer, so the web build renders without MSAA.
    set(MSENTARA_SAMPLE_COUNT 1 CACHE STRING "MSAA sample count")
else()
    set(MSENTARA_WEB FALSE)
    set(MSENTARA_SAMPLE_COUNT 4 CACHE STRING "MSAA sample count")
endif()

message(STATUS "MSentara: platform = ${CMAKE_SYSTEM_NAME}, web = ${MSENTARA_WEB}")

msentara_fetch_dependencies()

# ---------------------------------------------------------------------------
# Core: rendering-agnostic scene/camera/timeline data structures.
# ---------------------------------------------------------------------------
add_library(msentara_core STATIC
    src/core/Animation.cpp
    src/core/AudioAnalysis.cpp
    src/core/Camera.cpp
    src/core/Layer2D.cpp
    src/core/Mesh.cpp
    src/core/Morph.cpp
    src/core/OnionSkin.cpp
    src/core/Physics.cpp
    src/core/ProjectIO.cpp
    src/core/Scene.cpp
    src/core/Skeleton.cpp
    src/core/Timeline.cpp
    src/core/Transform.cpp
)
target_include_directories(msentara_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(msentara_core PUBLIC glm::glm)
# nlohmann/json is used only by ProjectIO.cpp, so it stays PRIVATE: the rest
# of core (and every test) keeps compiling without the heavy JSON header.
target_link_libraries(msentara_core PRIVATE nlohmann_json)
msentara_set_warnings(msentara_core)

# ---------------------------------------------------------------------------
# Formats: importers that translate on-disk formats into core structures.
# ---------------------------------------------------------------------------
add_library(msentara_formats STATIC
    src/formats/PmxLoader.cpp
    src/formats/ShiftJis.cpp
    src/formats/VmdLoader.cpp
)
target_include_directories(msentara_formats PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(msentara_formats PUBLIC msentara_core)
msentara_set_warnings(msentara_formats)

# ---------------------------------------------------------------------------
# Platform: sokol window, input and graphics wrapper.
# ---------------------------------------------------------------------------
add_library(msentara_platform STATIC
    src/platform/AudioEngine.cpp
    src/platform/ImageIO.cpp
    src/platform/Renderer.cpp
    src/platform/SokolImpl.cpp
)
target_include_directories(msentara_platform PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${SOKOL_INCLUDE_DIR}"
    "${IMGUI_INCLUDE_DIR}"
)
# stb_image is an implementation detail of the platform layer, so it stays
# PRIVATE.
target_include_directories(msentara_platform PRIVATE "${STB_INCLUDE_DIR}")
target_compile_definitions(msentara_platform PUBLIC
    MSENTARA_SAMPLE_COUNT=${MSENTARA_SAMPLE_COUNT}
)
target_link_libraries(msentara_platform PUBLIC
    msentara_core
    imgui
    imguizmo
    sokol
    miniaudio
)
msentara_set_warnings(msentara_platform)

# ---------------------------------------------------------------------------
# UI: Dear ImGui docking layout and ImGuizmo manipulator.
# ---------------------------------------------------------------------------
add_library(msentara_ui STATIC
    src/ui/Editor.cpp
    src/ui/ModelInstance.cpp
    src/ui/Scene2DInstance.cpp
    src/ui/Theme.cpp
)
target_include_directories(msentara_ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${IMGUI_INCLUDE_DIR}"
    "${IMGUIZMO_INCLUDE_DIR}"
)
target_link_libraries(msentara_ui PUBLIC msentara_platform msentara_formats)
msentara_set_warnings(msentara_ui)

# ---------------------------------------------------------------------------
# Application.
# ---------------------------------------------------------------------------
add_executable(MSentara src/main.cpp)
target_link_libraries(MSentara PRIVATE msentara_ui)
msentara_set_warnings(MSentara)

if(MSENTARA_WEB)
    set_target_properties(MSentara PROPERTIES
        SUFFIX ".html"
        # MIN/MAX_WEBGL_VERSION=2 force WebGL2 (GLES3); USE_WEBGL2/FULL_ES3
        # keep the GLES3 sokol backend and full ES3 shader syntax enabled.
        LINK_FLAGS "-sUSE_WEBGL2=1 -sFULL_ES3=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
                    -sALLOW_MEMORY_GROWTH=1 \
                    -sEXIT_RUNTIME=0 -sSTACK_SIZE=1048576 \
                    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/cmake/web/shell.html"
    )
else()
    # Desktop GL: the sokol GL backend dlopens libGL, so link the system GL
    # only for the platform-level X11/EGL symbols it needs.
    if(UNIX AND NOT APPLE)
        find_package(Threads REQUIRED)
        find_package(OpenGL REQUIRED)
        # sokol_app's X11 backend links these directly on Linux.
        find_package(X11 REQUIRED)
        target_link_libraries(msentara_platform PUBLIC
            Threads::Threads
            OpenGL::GL
            X11::X11
            dl
            m
        )
        # Xcursor and Xi are not always exported as imported targets.
        foreach(x_lib Xcursor Xi Xext)
            find_library(MSENTARA_${x_lib}_LIBRARY ${x_lib})
            if(MSENTARA_${x_lib}_LIBRARY)
                target_link_libraries(msentara_platform PUBLIC
                    ${MSENTARA_${x_lib}_LIBRARY})
            endif()
        endforeach()
    elseif(APPLE)
        find_library(COCOA_LIBRARY Cocoa REQUIRED)
        find_library(IOKIT_LIBRARY IOKit REQUIRED)
        find_library(COREVIDEO_LIBRARY CoreVideo REQUIRED)
        find_library(METAL_LIBRARY Metal REQUIRED)
        target_link_libraries(msentara_platform PUBLIC
            ${COCOA_LIBRARY}
            ${IOKIT_LIBRARY}
            ${COREVIDEO_LIBRARY}
            ${METAL_LIBRARY}
        )
    elseif(WIN32)
        target_link_libraries(msentara_platform PUBLIC
            opengl32
            gdi32
            user32
            ole32
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests (core layer only: no GPU, no window).
# ---------------------------------------------------------------------------
if(NOT MSENTARA_WEB)
    enable_testing()
    add_subdirectory(tests)
endif()
