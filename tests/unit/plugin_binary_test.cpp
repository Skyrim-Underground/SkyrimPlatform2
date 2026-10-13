// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "pe_image.h"
#include "sp2/build_info.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    // SKSEPluginVersionData (kVersion 1) from skse64/PluginAPI.h, deliberately not CommonLibSSE's own struct.
    struct SksePluginVersionData
    {
        std::uint32_t dataVersion;
        std::uint32_t pluginVersion;
        char name[256];
        char author[256];
        char supportEmail[252];
        std::uint32_t versionIndependenceEx;
        std::uint32_t versionIndependence;
        std::uint32_t compatibleVersions[16];
        std::uint32_t seVersionRequired;
    };
    static_assert(offsetof(SksePluginVersionData, versionIndependenceEx) == 0x304);
    static_assert(offsetof(SksePluginVersionData, versionIndependence) == 0x308);
    static_assert(offsetof(SksePluginVersionData, compatibleVersions) == 0x30C);
    static_assert(offsetof(SksePluginVersionData, seVersionRequired) == 0x34C);
    static_assert(sizeof(SksePluginVersionData) == 0x350);

    constexpr std::uint32_t kAddressLibraryPostAE = 1u << 0;
    constexpr std::uint32_t kStructsPost629 = 1u << 2;
    constexpr std::uint32_t kAddressLibraryV5 = 1u << 1;

    // SKSE's MAKE_EXE_VERSION_EX packing.
    constexpr std::uint32_t PackVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t build, std::uint32_t sub)
    {
        return ((major & 0xFF) << 24) | ((minor & 0xFF) << 16) | ((build & 0xFFF) << 4) | (sub & 0xF);
    }

    const sp2::test::PeImage& Plugin()
    {
        static const sp2::test::PeImage image{ SP2_PLUGIN_PATH };
        return image;
    }

    SksePluginVersionData VersionData()
    {
        const auto rva = Plugin().ExportRva("SKSEPlugin_Version");
        REQUIRE(rva.has_value());
        const auto bytes = Plugin().Bytes(*rva, sizeof(SksePluginVersionData));
        SksePluginVersionData data{};
        std::memcpy(&data, bytes.data(), sizeof(data));
        return data;
    }

    std::string_view FixedString(const char* text, std::size_t capacity)
    {
        return { text, strnlen(text, capacity) };
    }

    std::string Lowercase(std::string text)
    {
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }
}

TEST_CASE("plugin declares Address Library V5 and post-1.6.629 structs", "[plugin]")
{
    const auto data = VersionData();

    CHECK(data.dataVersion == 1);
    CHECK(data.versionIndependence == (kAddressLibraryPostAE | kStructsPost629));
    CHECK(data.versionIndependenceEx == kAddressLibraryV5);
    CHECK(data.seVersionRequired == PackVersion(2, 3, 1, 0));
    CHECK(std::ranges::all_of(data.compatibleVersions, [](std::uint32_t version) { return version == 0; }));
}

TEST_CASE("plugin declaration carries the product name, vendor and version", "[plugin]")
{
    const auto data = VersionData();

    CHECK(FixedString(data.name, sizeof(data.name)) == sp2::build::kProductName);
    CHECK(FixedString(data.author, sizeof(data.author)) == sp2::build::kVendor);
    CHECK(FixedString(data.supportEmail, sizeof(data.supportEmail)).empty());
    CHECK(data.pluginVersion == PackVersion(sp2::build::kVersionMajor, sp2::build::kVersionMinor, sp2::build::kVersionPatch, 0));
}

TEST_CASE("plugin exports only SKSEPlugin_Version and SKSEPlugin_Load", "[plugin]")
{
    auto names = Plugin().ExportNames();
    std::ranges::sort(names);

    CHECK(names == std::vector<std::string>{ "SKSEPlugin_Load", "SKSEPlugin_Version" });
}

TEST_CASE("plugin imports Windows system DLLs only", "[plugin]")
{
    // An allowlist, not a System32 lookup: the dynamic CRT's ucrtbase.dll and api-ms-win-crt-* live there too.
    constexpr std::array<std::string_view, 5> kAllowed{ "kernel32.dll", "ole32.dll", "shell32.dll", "user32.dll", "version.dll" };
    // CommonLibSSE-NG's Win32 wrappers share one object file. Its Debug library is built without /Gy, so a Debug link keeps every wrapper and imports these as well; the Release link drops the unused ones.
    constexpr std::array<std::string_view, 6> kAllowedInDebug{ "advapi32.dll", "bcrypt.dll", "d3d11.dll", "d3dcompiler_47.dll", "dbghelp.dll", "dxgi.dll" };
#ifdef NDEBUG
    constexpr bool kDebugLink = false;
#else
    constexpr bool kDebugLink = true;
#endif

    const auto modules = Plugin().ImportedModules();
    REQUIRE_FALSE(modules.empty());
    for (const auto& module : modules) {
        INFO("import: " << module);
        const auto name = Lowercase(module);
        const bool allowed = std::ranges::find(kAllowed, name) != kAllowed.end() || (kDebugLink && std::ranges::find(kAllowedInDebug, name) != kAllowedInDebug.end());
        CHECK(allowed);
    }
}
