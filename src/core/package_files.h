// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sp2::core
{
    [[nodiscard]] std::optional<std::string> CheckPackagePath(std::string_view path);

    struct ReadError
    {
        bool missing = false;
        std::string message;
    };

    [[nodiscard]] std::expected<std::string, ReadError> ReadFileBelow(const std::filesystem::path& root, std::string_view path);
}
