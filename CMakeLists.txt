# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

cmake_minimum_required(VERSION 4.2)

project(SkyrimPlatform2 VERSION 0.1.0 LANGUAGES CXX RC)

option(SP2_DEV_BUILD "Developer build; release builds must leave this OFF" OFF)
set(SP2_VENDOR "Skyrim Underground" CACHE STRING "Vendor name stamped into the plugin declaration and version resource")
set(SP2_GATE_TEST_ALLOWLIST "" CACHE STRING "Dev builds only: game versions (a.b.c.d, ';'-separated) that replace the version gate's allowlist, for the refusal test")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

message(STATUS "SP2: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}, MSVC_TOOLSET_VERSION ${MSVC_TOOLSET_VERSION}, generator toolset '${CMAKE_VS_PLATFORM_TOOLSET}', SP2_DEV_BUILD=${SP2_DEV_BUILD}")

include(cmake/Warnings.cmake)
include(cmake/Version.cmake)

# CommonLibSSE's exported target links Microsoft::DirectXTK, but its package config does not find it.
find_package(directxtk CONFIG REQUIRED)
find_package(CommonLibSSE CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(qjs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

sp2_add_build_info()

add_subdirectory(src)
add_subdirectory(tools/sp2-run)

enable_testing()
add_subdirectory(tests)

include(cmake/Install.cmake)
