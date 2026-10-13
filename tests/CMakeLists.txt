# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(sp2_unit_tests
    unit/frame_stats_test.cpp
    unit/gate_test.cpp
    unit/host_loop_test.cpp
    unit/host_module_test.cpp
    unit/host_test_util.h
    unit/js_codegen_test.cpp
    unit/js_limits_test.cpp
    unit/js_module_test.cpp
    unit/js_test_util.h
    unit/js_value_test.cpp
    unit/json_test.cpp
    unit/log_test.cpp
    unit/manifest_test.cpp
    unit/package_files_test.cpp
    unit/package_test_util.cpp
    unit/package_test_util.h
    unit/packages_test.cpp
    unit/pe_image.cpp
    unit/pe_image.h
    unit/plugin_binary_test.cpp
    unit/quickjs_pin_test.cpp
    unit/settings_test.cpp
    unit/version_test.cpp
)
target_compile_definitions(sp2_unit_tests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    "SP2_PLUGIN_PATH=\"$<TARGET_FILE:SkyrimPlatform2>\""
    "SP2_TEST_WORK_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/work\""
)
target_link_libraries(sp2_unit_tests PRIVATE sp2_gate sp2_host Catch2::Catch2WithMain qjs)
add_dependencies(sp2_unit_tests SkyrimPlatform2)
sp2_set_warnings(sp2_unit_tests)

include(Catch)
# A Debug CRT assertion (QuickJS-ng checks for leaked script values when a runtime is freed) waits on a message box; the timeout turns that into a failure instead of a stuck CI job.
catch_discover_tests(sp2_unit_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 120)

# sp2-run on a packages folder, compared with an expected log and exit code by sp2-run/check.cmake.
function(sp2_add_run_test name packages expected)
    cmake_parse_arguments(PARSE_ARGV 3 run "" "SETTINGS;FRAMES;FRAME_MS;EXIT" "")
    set(settings "")
    if(DEFINED run_SETTINGS)
        set(settings "-DSETTINGS=${run_SETTINGS}")
    endif()
    add_test(
        NAME "${name}"
        COMMAND "${CMAKE_COMMAND}"
            "-DSP2_RUN=$<TARGET_FILE:sp2-run>"
            "-DPACKAGES=${packages}"
            ${settings}
            "-DFRAMES=${run_FRAMES}"
            "-DFRAME_MS=${run_FRAME_MS}"
            "-DEXPECTED=${expected}"
            "-DEXPECTED_EXIT=${run_EXIT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/sp2-run/check.cmake"
    )
    set_tests_properties("${name}" PROPERTIES TIMEOUT 120)
endfunction()

sp2_add_run_test("sp2-run: samples/ for 10 frames of 20 ms matches tests/sp2-run/samples.expected.txt"
    "${PROJECT_SOURCE_DIR}/samples" "${CMAKE_CURRENT_SOURCE_DIR}/sp2-run/samples.expected.txt"
    SETTINGS "${CMAKE_CURRENT_SOURCE_DIR}/sp2-run/settings" FRAMES 10 FRAME_MS 20 EXIT 0)
sp2_add_run_test("sp2-run: tests/sp2-run/isolation/ for 3 frames exits 1 and matches tests/sp2-run/isolation.expected.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/sp2-run/isolation" "${CMAKE_CURRENT_SOURCE_DIR}/sp2-run/isolation.expected.txt"
    FRAMES 3 FRAME_MS 20 EXIT 1)
