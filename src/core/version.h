// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace sp2::core
{
    // Four-part version, compared field by field: 1.7.104.0.
    using Version = std::array<std::uint16_t, 4>;

    [[nodiscard]] std::string ToString(const Version& version);

    // SKSE's MAKE_EXE_VERSION_EX packing: 8-bit major, 8-bit minor, 12-bit build, 4-bit sub.
    [[nodiscard]] constexpr Version UnpackSkseVersion(std::uint32_t packed) noexcept
    {
        return {
            static_cast<std::uint16_t>((packed >> 24) & 0xFF),
            static_cast<std::uint16_t>((packed >> 16) & 0xFF),
            static_cast<std::uint16_t>((packed >> 4) & 0xFFF),
            static_cast<std::uint16_t>(packed & 0xF),
        };
    }

    // FILEVERSION from a PE file's VERSIONINFO resource; nullopt if the file has none or cannot be read.
    [[nodiscard]] std::optional<Version> ReadFileVersion(const std::filesystem::path& file);
}
