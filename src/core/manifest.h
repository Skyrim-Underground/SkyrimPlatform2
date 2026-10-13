// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "core/json.h"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace sp2::core
{
    inline constexpr std::string_view kManifestFileName = "sp2.json";
    inline constexpr std::uint32_t kDefaultMemoryLimitMiB = 256;
    inline constexpr std::uint32_t kMinMemoryLimitMiB = 16;
    inline constexpr std::uint32_t kMaxMemoryLimitMiB = 2048;

    // A package's sp2.json once validated (decision 0005, section 3 Packages; amendment A2: no file hashes). The native
    // module list arrives with M7.
    struct Manifest
    {
        std::string name;
        // MAJOR.MINOR.PATCH
        std::string version;
        // The package's ES module, a package path (CheckPackagePath) ending in .js.
        std::string main;
        // Defaults of the package's settings, an object.
        Json settings = Json::object();
        std::uint32_t memoryLimitMiB = kDefaultMemoryLimitMiB;
    };

    // 1 to 64 characters of a-z, 0-9 and '-', and not a Windows device name (con, prn, aux, nul, com0-com9, lpt0-lpt9).
    [[nodiscard]] bool IsPackageName(std::string_view name);

    [[nodiscard]] std::expected<Manifest, std::vector<std::string>> ParseManifest(std::string_view text, std::string_view folderName, std::uint32_t apiMajor);
}
