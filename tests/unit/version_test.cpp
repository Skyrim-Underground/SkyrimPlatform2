// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/paths.h"
#include "core/version.h"
#include "package_test_util.h"
#include "sp2/build_info.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using sp2::core::Version;

TEST_CASE("versions print as four dotted fields", "[version]")
{
    CHECK(sp2::core::ToString(Version{ 1, 7, 104, 0 }) == "1.7.104.0");
    CHECK(sp2::core::ToString(Version{ 65535, 0, 4095, 15 }) == "65535.0.4095.15");
}

TEST_CASE("SKSE's packed versions unpack field by field", "[version]")
{
    // Values from skse64_common/skse_version.h at SKSE v2.3.1.
    STATIC_CHECK(sp2::core::UnpackSkseVersion(0x02030010) == Version{ 2, 3, 1, 0 });
    STATIC_CHECK(sp2::core::UnpackSkseVersion(0x01070680) == Version{ 1, 7, 104, 0 });
    STATIC_CHECK(sp2::core::UnpackSkseVersion(0x01070630) == Version{ 1, 7, 99, 0 });
    STATIC_CHECK(sp2::core::UnpackSkseVersion(0x010649B1) == Version{ 1, 6, 1179, 1 });
}

TEST_CASE("file version is read from a PE's VERSIONINFO", "[version]")
{
    CHECK(sp2::core::ReadFileVersion(SP2_PLUGIN_PATH) == Version{ sp2::build::kVersionMajor, sp2::build::kVersionMinor, sp2::build::kVersionPatch, 0 });
}

TEST_CASE("file version is absent for a missing file or a file without VERSIONINFO", "[version]")
{
    const sp2::test::WorkFolder work("version-absent");
    const auto plain = work.Write("plain.txt", "not a PE file");

    CHECK_FALSE(sp2::core::ReadFileVersion(work / "missing.exe").has_value());
    CHECK_FALSE(sp2::core::ReadFileVersion(plain).has_value());
}

TEST_CASE("executable path names the running process", "[paths]")
{
    const auto path = sp2::core::ExecutablePath();

    REQUIRE(path.has_value());
    CHECK(path->filename() == "sp2_unit_tests.exe");
    CHECK(std::filesystem::is_regular_file(*path));
}

TEST_CASE("Address Library path follows SKSE's and CommonLibSSE-NG's naming", "[paths]")
{
    CHECK(sp2::core::AddressLibraryPath(Version{ 1, 7, 104, 0 }).generic_string() == "Data/SKSE/Plugins/versionlib-1-7-104-0.bin");
    CHECK(sp2::core::AddressLibraryPath(Version{ 1, 7, 99, 0 }).filename() == "versionlib-1-7-99-0.bin");
}

TEST_CASE("output folder is SKSE/SkyrimPlatform2 under the Steam game's documents folder", "[paths]")
{
    const std::filesystem::path documents = std::filesystem::path(SP2_TEST_WORK_DIR) / "Documents";

    CHECK(sp2::core::OutputDirectory(documents) == documents / "My Games" / "Skyrim Special Edition" / "SKSE" / "SkyrimPlatform2");
}

TEST_CASE("profile folder resolves to an existing directory", "[paths]")
{
    const auto profile = sp2::core::ProfileDirectory();

    REQUIRE(profile.has_value());
    CHECK(profile->is_absolute());
    CHECK(std::filesystem::is_directory(*profile));
}
