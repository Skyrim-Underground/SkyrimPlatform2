// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/version.h"

#include <format>
#include <vector>

#include <Windows.h>

namespace sp2::core
{
    std::string ToString(const Version& version)
    {
        return std::format("{}.{}.{}.{}", version[0], version[1], version[2], version[3]);
    }

    std::optional<Version> ReadFileVersion(const std::filesystem::path& file)
    {
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &ignored);
        if (size == 0) {
            return std::nullopt;
        }
        std::vector<std::byte> data(size);
        if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) {
            return std::nullopt;
        }
        void* value = nullptr;
        UINT length = 0;
        if (!VerQueryValueW(data.data(), L"\\", &value, &length) || value == nullptr || length < sizeof(VS_FIXEDFILEINFO)) {
            return std::nullopt;
        }
        const auto* info = static_cast<const VS_FIXEDFILEINFO*>(value);
        if (info->dwSignature != VS_FFI_SIGNATURE) {
            return std::nullopt;
        }
        return Version{ HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS) };
    }
}
