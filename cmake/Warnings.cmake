# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

# Warnings are errors for SP2's own code; headers from dependencies are compiled at /external:W0.
function(sp2_set_warnings target)
    target_compile_options(${target} PRIVATE
        /W4
        /permissive-
        /utf-8
        /Zc:__cplusplus
        /external:anglebrackets
        /external:W0
    )
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
endfunction()
