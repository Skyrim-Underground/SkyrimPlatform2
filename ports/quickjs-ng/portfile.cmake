# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# Tag v0.17.0 is commit 6d46d07d04041b40f4f49eaa7fdebe44c314c699.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO quickjs-ng/quickjs
    REF "v${VERSION}"
    SHA512 e4f9bffa51ebe8c3a4dba75c62df52e28ee4af31bf81d66694b87748ba4db754e3e7c948fa8c40f3badc045caa0506ed7ef333b40dd4bfe42675ca2ffc5d625d
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DQJS_BUILD_LIBC=OFF
        -DQJS_BUILD_EXAMPLES=OFF
        -DQJS_BUILD_CLI_WITH_MIMALLOC=OFF
        -DQJS_BUILD_CLI_WITH_STATIC_MIMALLOC=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME qjs CONFIG_PATH lib/cmake/qjs)

# Upstream always installs the qjs and qjsc executables, man pages and examples; SP2 uses the library only.
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/bin"
    "${CURRENT_PACKAGES_DIR}/debug/bin"
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/share/doc"
    "${CURRENT_PACKAGES_DIR}/share/man"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
