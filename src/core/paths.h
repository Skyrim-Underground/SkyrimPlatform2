// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "core/version.h"

#include <filesystem>
#include <optional>

namespace sp2::core
{
    // sp2_core does not link NG: the tests and sp2-run stay game-free.
    [[nodiscard]] std::filesystem::path OutputDirectory(const std::filesystem::path& documents);

    // OutputDirectory under the current user's Documents known folder; nullopt if Windows cannot resolve it.
    [[nodiscard]] std::optional<std::filesystem::path> OutputDirectory();

    // The current user's profile folder, which log redaction replaces; nullopt if Windows cannot resolve it.
    [[nodiscard]] std::optional<std::filesystem::path> ProfileDirectory();

    // Full path of the running process's executable; used to read its version, never logged.
    [[nodiscard]] std::optional<std::filesystem::path> ExecutablePath();

    // Address Library database for a game version, relative to the game folder, named as SKSE and CommonLibSSE-NG look it up: Data/SKSE/Plugins/versionlib-1-7-104-0.bin.
    [[nodiscard]] std::filesystem::path AddressLibraryPath(const Version& game);
}
