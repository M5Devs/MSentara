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
#
# imgui and sokol ship no CMakeLists.txt, so we add the targets ourselves.
# imguizmo does ship one, but it drags in its own example/imgui fetch, so we
# build the single translation unit we need directly instead.

include(FetchContent)

# Pinned revisions keep the build reproducible; bump deliberately.
set(MSENTARA_SOKOL_TAG    "f5e8833408804652da04e532067e7695bda46118" CACHE STRING "floooh/sokol revision")
set(MSENTARA_IMGUI_TAG    "9dbcb2f1cc0e8a3ffac51d7f9d28efdb228db14b" CACHE STRING "ocornut/imgui docking revision")
set(MSENTARA_IMGUIZMO_TAG "105e4fde45e4e74aae8973d98750820ef7aca7e2" CACHE STRING "CedricGuillemet/ImGuizmo revision")
set(MSENTARA_GLM_TAG      "1.0.1"                                    CACHE STRING "g-truc/glm tag")
set(MSENTARA_STB_TAG      "master"                                   CACHE STRING "nothings/stb revision")

macro(msentara_fetch_dependencies)
    FetchContent_Declare(sokol
        GIT_REPOSITORY https://github.com/floooh/sokol.git
        GIT_TAG        ${MSENTARA_SOKOL_TAG}
        GIT_SHALLOW    TRUE
    )
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        ${MSENTARA_IMGUI_TAG}
        GIT_SHALLOW    TRUE
    )
    # ImGuizmo ships a CMakeLists.txt that would add its example and fetch its
    # own imgui, colliding with ours. Pointing SOURCE_SUBDIR at a directory
    # that does not exist makes MakeAvailable populate only, with no
    # add_subdirectory().
    FetchContent_Declare(imguizmo
        GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
        GIT_TAG        ${MSENTARA_IMGUIZMO_TAG}
        GIT_SHALLOW    TRUE
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
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  msentara-no-cmake-subdir
    )

    # sokol and imgui ship no CMakeLists.txt, so MakeAvailable populates them
    # without adding a subdirectory; we add the targets ourselves below.
    FetchContent_MakeAvailable(sokol imgui imguizmo glm stb)

    set(SOKOL_INCLUDE_DIR "${sokol_SOURCE_DIR}" CACHE INTERNAL "sokol include dir")
    set(IMGUI_INCLUDE_DIR "${imgui_SOURCE_DIR}" CACHE INTERNAL "imgui include dir")
    set(IMGUIZMO_INCLUDE_DIR "${imguizmo_SOURCE_DIR}/src" CACHE INTERNAL "ImGuizmo include dir")
    set(STB_INCLUDE_DIR "${stb_SOURCE_DIR}" CACHE INTERNAL "stb include dir")

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
