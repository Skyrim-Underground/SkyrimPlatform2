# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

# Development and CI only; never shipped, so there is no install rule.
add_executable(sp2-run main.cpp)
target_link_libraries(sp2-run PRIVATE sp2_host)
sp2_set_warnings(sp2-run)
