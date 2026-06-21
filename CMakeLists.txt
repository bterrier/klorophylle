# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)

project(Klorophylle
    VERSION 0.0.1
    DESCRIPTION "Plant-first plant-monitoring app"
    LANGUAGES CXX)

# C++23 is the baseline (std::expected error backbone). See docs/adr/0003.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6.10+ baseline — verified locally against 6.10.3 / 6.11.1. See docs/adr/0003.
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Bluetooth Sql Graphs Test DBus Svg)
qt_standard_project_setup(REQUIRES 6.10)
set(CMAKE_AUTOMOC ON)

# Single source of version truth -> generated header (kills the six-place
# duplication).
configure_file(cmake/version.h.in "${CMAKE_BINARY_DIR}/generated/klr/version.h" @ONLY)
add_library(klr_version INTERFACE)
target_include_directories(klr_version INTERFACE "${CMAKE_BINARY_DIR}/generated")

enable_testing()

add_subdirectory(src/karness)
add_subdirectory(src/core)
add_subdirectory(src/persistence)
add_subdirectory(src/agent)
add_subdirectory(src/devices)
add_subdirectory(src/ble)
add_subdirectory(src/style)
add_subdirectory(src/gui)
add_subdirectory(src/app)
add_subdirectory(tests)
