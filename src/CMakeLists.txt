# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

add_library(sp2_core STATIC
    core/json.cpp
    core/json.h
    core/log.cpp
    core/log.h
    core/manifest.cpp
    core/manifest.h
    core/package_files.cpp
    core/package_files.h
    core/paths.cpp
    core/paths.h
    core/version.cpp
    core/version.h
)
target_include_directories(sp2_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(sp2_core
    PUBLIC JSON_USE_IMPLICIT_CONVERSIONS=0
    PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_libraries(sp2_core
    PUBLIC sp2_build_info spdlog::spdlog nlohmann_json::nlohmann_json
    PRIVATE shell32 ole32 uuid version
)
sp2_set_warnings(sp2_core)

add_library(sp2_gate STATIC
    plugin/gate.cpp
    plugin/gate.h
)
target_link_libraries(sp2_gate PUBLIC sp2_core)
sp2_set_warnings(sp2_gate)

# The sp2::js interface with its one engine backend; nothing outside src/engine/ sees the engine's headers.
add_library(sp2_js STATIC
    host/js/js.h
    host/js/no_codegen.h
    engine/quickjs/backend.h
    engine/quickjs/runtime.cpp
    engine/quickjs/value.cpp
)
target_include_directories(sp2_js PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sp2_js PRIVATE qjs)
sp2_set_warnings(sp2_js)

# The script host: host globals, the skyrimPlatform2 module, module rules, the event loop, and packages.
add_library(sp2_host STATIC
    host/frame_stats.cpp
    host/frame_stats.h
    host/inbox.h
    host/packages.cpp
    host/packages.h
    host/script_host.cpp
    host/script_host.h
    host/settings.cpp
    host/settings.h
    host/timers.cpp
    host/timers.h
)
target_link_libraries(sp2_host PUBLIC sp2_core sp2_js spdlog::spdlog)
sp2_set_warnings(sp2_host)

add_library(SkyrimPlatform2 SHARED
    plugin/plugin.cpp
)
target_compile_definitions(SkyrimPlatform2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SkyrimPlatform2 PRIVATE sp2_gate CommonLibSSE::CommonLibSSE)
sp2_set_warnings(SkyrimPlatform2)
sp2_add_version_resource(SkyrimPlatform2 "SkyrimPlatform2 SKSE plugin")
