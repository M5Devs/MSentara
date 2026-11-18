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

# Fetches and configures every third-party dependency, and exposes the
# resulting include directories as cache variables:
#
#   SOKOL_INCLUDE_DIR   - directory containing sokol_app.h / sokol_gfx.h
#   IMGUI_INCLUDE_DIR   - directory containing imgui.h
#   IMGUIZMO_INCLUDE_DIR- directory containing ImGuizmo.h
#   NLOHMANN_JSON_INCLUDE_DIR - directory containing nlohmann/json.hpp
#
# imgui and sokol ship no CMakeLists.txt, so we add the targets ourselves.
# imguizmo does ship one, but it drags in its own example/imgui fetch, so we
# build the single translation unit we need directly instead.

include(FetchContent)

# Pinned revisions keep the build reproducible; bump deliberately.
#
# NOTE: do NOT add GIT_SHALLOW to the commit-pinned clones. FetchContent's
# shallow path runs `git clone --depth 1 --no-single-branch` and then
# `git checkout <sha>`; a commit that is not the *tip* of some branch (sokol's
# is not) is absent from that shallow history and the checkout dies with
# "unable to read tree". A plain (full) clone always contains the pinned
# commit, so a fresh configure works with no FETCHCONTENT_SOURCE_DIR_*
# overrides. glm is pinned to a real tag (a tag *is* a tip, so its shallow
# clone is safe and fast) and nlohmann/json is a release-asset download.
set(MSENTARA_SOKOL_TAG    "f5e8833408804652da04e532067e7695bda46118" CACHE STRING "floooh/sokol revision")
set(MSENTARA_IMGUI_TAG    "9dbcb2f1cc0e8a3ffac51d7f9d28efdb228db14b" CACHE STRING "ocornut/imgui docking revision")
set(MSENTARA_IMGUIZMO_TAG "105e4fde45e4e74aae8973d98750820ef7aca7e2" CACHE STRING "CedricGuillemet/ImGuizmo revision")
set(MSENTARA_GLM_TAG      "1.0.1"                                    CACHE STRING "g-truc/glm tag")
set(MSENTARA_STB_TAG      "9600522de53973336fa033a289858a4354cbb04c" CACHE STRING "nothings/stb revision")
set(MSENTARA_JSON_TAG     "v3.11.3"                                  CACHE STRING "nlohmann/json release")

macro(msentara_fetch_dependencies)
    FetchContent_Declare(sokol
        GIT_REPOSITORY https://github.com/floooh/sokol.git
        GIT_TAG        ${MSENTARA_SOKOL_TAG}
    )
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        ${MSENTARA_IMGUI_TAG}
    )
    # ImGuizmo ships a CMakeLists.txt that would add its example and fetch its
    # own imgui, colliding with ours. Pointing SOURCE_SUBDIR at a directory
    # that does not exist makes MakeAvailable populate only, with no
    # add_subdirectory().
    FetchContent_Declare(imguizmo
        GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
        GIT_TAG        ${MSENTARA_IMGUIZMO_TAG}
        SOURCE_SUBDIR  msentara-no-cmake-subdir
    )
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        ${MSENTARA_GLM_TAG}
        GIT_SHALLOW    TRUE
    )
    # stb is a bag of single-header libraries with no build system. We only
    # need stb_image.h, which the platform layer compiles via STB_IMAGE_IMPLEMENTATION.
    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        ${MSENTARA_STB_TAG}
        SOURCE_SUBDIR  msentara-no-cmake-subdir
    )
    # nlohmann/json is header-only. The release asset is a small `include/`
    # zip, far lighter than cloning the repository's full history, and it
    # carries no CMakeLists.txt so nothing extra is added.
    FetchContent_Declare(nlohmann_json
        URL      https://github.com/nlohmann/json/releases/download/${MSENTARA_JSON_TAG}/include.zip
        URL_HASH SHA256=a22461d13119ac5c78f205d3df1db13403e58ce1bb1794edc9313677313f4a9d
    )

    # sokol and imgui ship no CMakeLists.txt, so MakeAvailable populates them
    # without adding a subdirectory; we add the targets ourselves below.
    FetchContent_MakeAvailable(sokol imgui imguizmo glm stb nlohmann_json)

    set(SOKOL_INCLUDE_DIR "${sokol_SOURCE_DIR}" CACHE INTERNAL "sokol include dir")
    set(IMGUI_INCLUDE_DIR "${imgui_SOURCE_DIR}" CACHE INTERNAL "imgui include dir")
    set(IMGUIZMO_INCLUDE_DIR "${imguizmo_SOURCE_DIR}/src" CACHE INTERNAL "ImGuizmo include dir")
    set(STB_INCLUDE_DIR "${stb_SOURCE_DIR}" CACHE INTERNAL "stb include dir")
    set(NLOHMANN_JSON_INCLUDE_DIR "${nlohmann_json_SOURCE_DIR}/include" CACHE INTERNAL "nlohmann/json include dir")

    # --- nlohmann/json ----------------------------------------------------
    # Header-only; expose the include directory through a conventional target
    # name so consumers link `nlohmann_json` like they would upstream.
    if(NOT TARGET nlohmann_json)
        add_library(nlohmann_json INTERFACE)
        target_include_directories(nlohmann_json SYSTEM INTERFACE
            "${NLOHMANN_JSON_INCLUDE_DIR}"
        )
    endif()

    # --- Dear ImGui -------------------------------------------------------
    if(NOT TARGET imgui)
        add_library(imgui STATIC
            "${imgui_SOURCE_DIR}/imgui.cpp"
            "${imgui_SOURCE_DIR}/imgui_draw.cpp"
            "${imgui_SOURCE_DIR}/imgui_tables.cpp"
            "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
            "${imgui_SOURCE_DIR}/imgui_demo.cpp"
        )
        target_include_directories(imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
        set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()

    # --- ImGuizmo ---------------------------------------------------------
    if(NOT TARGET imguizmo)
        add_library(imguizmo STATIC "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
        target_include_directories(imguizmo SYSTEM PUBLIC
            "${imguizmo_SOURCE_DIR}/src"
            "${imgui_SOURCE_DIR}"
        )
        target_link_libraries(imguizmo PUBLIC imgui)
        set_target_properties(imguizmo PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()

    # --- sokol ------------------------------------------------------------
    # Header-only: the single implementation lives in
    # src/platform/SokolImpl.cpp, so this is just an include-path carrier.
    if(NOT TARGET sokol)
        add_library(sokol INTERFACE)
        target_include_directories(sokol SYSTEM INTERFACE
            "${sokol_SOURCE_DIR}"
            "${sokol_SOURCE_DIR}/util"
        )
    endif()
endmacro()
