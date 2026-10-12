# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# Tag v12.1.0 is commit 8ffc2b74af015f837de53cc02b0cb470a15678e4.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/CommonLibSSE-NG
    REF "v${VERSION}"
    SHA512 4a2e9a95d34e5e19756ca354496d7e5063ea2c13edbf710e4520c2734771b76a9bfd9e66b5beb2d46f5e79b66b8e3e2c08d8acadfc5b7bb0b8458fb544fcce53
)

# AE only. Xbyak, patch safety (fetches MinHook by tag), REX config parsers, tests and IPO stay off.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DENABLE_SKYRIM_AE=ON
        -DENABLE_SKYRIM_SE=OFF
        -DENABLE_SKYRIM_VR=OFF
        -DBUILD_TESTS=OFF
        -DSKSE_SUPPORT_XBYAK=OFF
        -DSKSE_SUPPORT_PATCH_SAFETY=OFF
        -DREX_OPTION_INI=OFF
        -DREX_OPTION_JSON=OFF
        -DREX_OPTION_TOML=OFF
        -DCOMMONLIB_ENABLE_IPO=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake/CommonLibSSE)

# CommonLibSSEConfig.cmake includes this file, but upstream's install rules do not install it.
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")

# Drops the VR-only openvr_api.lib entry: disabled with VR off, but it holds an absolute build-tree path.
vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE-targets.cmake"
    ";\\$<\\$<BOOL:OFF>:${SOURCE_PATH}/extern/openvr/lib/win64/openvr_api.lib>"
    ""
)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.txt" "${SOURCE_PATH}/EXCEPTIONS.md")
