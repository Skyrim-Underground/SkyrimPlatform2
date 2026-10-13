# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

# Runs sp2-run on a packages folder and compares its exit code and standard output with the expected ones; line endings are normalised.
# cmake -DSP2_RUN=<exe> -DPACKAGES=<folder> [-DSETTINGS=<folder>] -DFRAMES=<count> -DFRAME_MS=<ms> -DEXPECTED=<file> -DEXPECTED_EXIT=<code> -P check.cmake
set(arguments --frames "${FRAMES}" --frame-ms "${FRAME_MS}")
if(DEFINED SETTINGS)
    list(APPEND arguments --settings "${SETTINGS}")
endif()
execute_process(
    COMMAND "${SP2_RUN}" ${arguments} "${PACKAGES}"
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE errors
    RESULT_VARIABLE result
)
file(READ "${EXPECTED}" expected)
string(REPLACE "\r\n" "\n" actual "${actual}")
string(REPLACE "\r\n" "\n" expected "${expected}")

if(NOT result STREQUAL EXPECTED_EXIT)
    message(FATAL_ERROR "sp2-run exited with ${result}, expected ${EXPECTED_EXIT}\n${errors}${actual}")
endif()
if(NOT actual STREQUAL expected)
    # Indented lines are printed as they are; CMake reflows the others.
    string(REPLACE "\n" "\n    " shownExpected "    ${expected}")
    string(REPLACE "\n" "\n    " shownActual "    ${actual}")
    message(FATAL_ERROR "sp2-run output differs from ${EXPECTED}\n  expected:\n${shownExpected}\n  actual:\n${shownActual}\n${errors}")
endif()
