# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

# Install tree mirrors the game's Data folder (decision 0005, section 9). PDBs and import libraries are not shipped.
install(FILES "$<TARGET_FILE:SkyrimPlatform2>" DESTINATION Data/SKSE/Plugins)

# Licences of the third-party code linked into SkyrimPlatform2.dll, as vcpkg installs them. The list follows the
# linker map of the M1 plugin; a dependency that starts being linked into the DLL is added here.
set(notices "${PROJECT_BINARY_DIR}/THIRD_PARTY_NOTICES")
file(WRITE "${notices}" "Third-party code linked into SkyrimPlatform2.dll. Each section holds that component's licence.\n")
foreach(port IN ITEMS commonlibsse-ng spdlog fmt)
    file(READ "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${port}/copyright" text)
    file(APPEND "${notices}" "\n\n======== ${port} ========\n\n${text}")
endforeach()

install(
    FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/NOTICE"
        "${PROJECT_SOURCE_DIR}/PRIVACY.md"
        "${notices}"
    DESTINATION Data/SkyrimPlatform2
)
