# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

# Interface target sp2_build_info: generated sp2/build_info.h plus the SP2_DEV_BUILD definition (0 or 1).
function(sp2_add_build_info)
    set(SP2_GATE_TEST_ALLOWLIST_SIZE 0)
    set(SP2_GATE_TEST_ALLOWLIST_INIT "")
    if(NOT SP2_GATE_TEST_ALLOWLIST STREQUAL "")
        # A release build must never start on a version outside the compiled allowlist.
        if(NOT SP2_DEV_BUILD)
            message(FATAL_ERROR "SP2_GATE_TEST_ALLOWLIST is for dev builds only (SP2_DEV_BUILD=ON)")
        endif()
        foreach(version IN LISTS SP2_GATE_TEST_ALLOWLIST)
            if(NOT version MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
                message(FATAL_ERROR "SP2_GATE_TEST_ALLOWLIST: '${version}' is not a four-part version such as 1.7.99.0")
            endif()
            set(parts "")
            foreach(index RANGE 1 4)
                math(EXPR part "${CMAKE_MATCH_${index}}")
                if(part GREATER 65535)
                    message(FATAL_ERROR "SP2_GATE_TEST_ALLOWLIST: '${version}' has a part above 65535")
                endif()
                list(APPEND parts "${part}")
            endforeach()
            list(JOIN parts ", " parts)
            string(APPEND SP2_GATE_TEST_ALLOWLIST_INIT "{ ${parts} }, ")
            math(EXPR SP2_GATE_TEST_ALLOWLIST_SIZE "${SP2_GATE_TEST_ALLOWLIST_SIZE} + 1")
        endforeach()
        message(STATUS "SP2: gate test build, allowlist ${SP2_GATE_TEST_ALLOWLIST}")
    endif()

    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/build_info.h.in"
        "${PROJECT_BINARY_DIR}/generated/sp2/build_info.h"
        @ONLY
    )
    add_library(sp2_build_info INTERFACE)
    target_include_directories(sp2_build_info INTERFACE "${PROJECT_BINARY_DIR}/generated")
    target_compile_definitions(sp2_build_info INTERFACE "SP2_DEV_BUILD=$<BOOL:${SP2_DEV_BUILD}>")
endfunction()

# Adds a VERSIONINFO resource carrying the project version and vendor to a DLL target.
function(sp2_add_version_resource target description)
    set(SP2_RC_DESCRIPTION "${description}")
    set(SP2_RC_FILENAME "${target}.dll")
    set(rc_file "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/version.rc.in" "${rc_file}" @ONLY)
    target_sources(${target} PRIVATE "${rc_file}")
endfunction()
