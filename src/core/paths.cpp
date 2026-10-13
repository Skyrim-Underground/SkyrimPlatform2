// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/paths.h"

#include <format>
#include <memory>
#include <string>

#include <Windows.h>
#include <KnownFolders.h>
#include <ShlObj.h>

namespace sp2::core
{
    namespace
    {
        std::optional<std::filesystem::path> KnownFolder(const KNOWNFOLDERID& id)
        {
            PWSTR folder = nullptr;
            const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &folder);
            // The buffer must be freed whether or not the call succeeded.
            const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(folder, &CoTaskMemFree);
            if (FAILED(result) || folder == nullptr) {
                return std::nullopt;
            }
            return std::filesystem::path(folder);
        }
    }

    std::filesystem::path OutputDirectory(const std::filesystem::path& documents)
    {
        return documents / "My Games" / "Skyrim Special Edition" / "SKSE" / "SkyrimPlatform2";
    }

    std::optional<std::filesystem::path> OutputDirectory()
    {
        return KnownFolder(FOLDERID_Documents).transform([](const std::filesystem::path& documents) { return OutputDirectory(documents); });
    }

    std::optional<std::filesystem::path> ProfileDirectory()
    {
        return KnownFolder(FOLDERID_Profile);
    }

    std::optional<std::filesystem::path> ExecutablePath()
    {
        // 32,767 characters is the extended-length path limit, so one call cannot truncate.
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return std::nullopt;
        }
        buffer.resize(length);
        return std::filesystem::path(buffer);
    }

    std::filesystem::path AddressLibraryPath(const Version& game)
    {
        return std::filesystem::path("Data") / "SKSE" / "Plugins" / std::format("versionlib-{}-{}-{}-{}.bin", game[0], game[1], game[2], game[3]);
    }
}
