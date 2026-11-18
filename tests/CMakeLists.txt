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

# Unit tests for the rendering-agnostic core layer. Each test is a standalone
# executable registered with CTest; no GPU or window is required.

set(MSENTARA_CORE_TESTS
    test_camera
    test_mesh
    test_scene
    test_skeleton
    test_timeline
    test_transform
)

foreach(test_name IN LISTS MSENTARA_CORE_TESTS)
    add_executable(${test_name} "${test_name}.cpp")
    target_include_directories(${test_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
    )
    target_link_libraries(${test_name} PRIVATE msentara_core)
    msentara_set_warnings(${test_name})

    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        LABELS "core"
        TIMEOUT 60
    )
endforeach()

# Format parsers need the formats library (and, transitively, core).
add_executable(test_pmx test_pmx.cpp)
target_include_directories(test_pmx PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(test_pmx PRIVATE msentara_formats)
msentara_set_warnings(test_pmx)

add_test(NAME test_pmx COMMAND test_pmx)
set_tests_properties(test_pmx PROPERTIES
    LABELS "formats"
    TIMEOUT 60
)

add_executable(test_vmd test_vmd.cpp)
target_include_directories(test_vmd PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(test_vmd PRIVATE msentara_formats)
msentara_set_warnings(test_vmd)

add_test(NAME test_vmd COMMAND test_vmd)
set_tests_properties(test_vmd PROPERTIES
    LABELS "formats"
    TIMEOUT 60
)
