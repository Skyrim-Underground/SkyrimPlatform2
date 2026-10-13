// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Windows.h>

namespace sp2::test
{
    // Bounds-checked view of a PE32+ file on disk that never loads the image; malformed input throws std::runtime_error.
    class PeImage
    {
    public:
        explicit PeImage(const std::filesystem::path& path);

        [[nodiscard]] std::vector<std::string> ExportNames() const;
        [[nodiscard]] std::optional<std::uint32_t> ExportRva(std::string_view name) const;

        // Normal and delay-load imports, as dumpbin /dependents lists them.
        [[nodiscard]] std::vector<std::string> ImportedModules() const;

        [[nodiscard]] std::span<const std::byte> Bytes(std::uint32_t rva, std::size_t size) const;

    private:
        template <class T>
        [[nodiscard]] T Read(std::size_t offset) const;

        // Every named export with its function RVA, in name-table order.
        [[nodiscard]] std::vector<std::pair<std::string, std::uint32_t>> Exports() const;
        [[nodiscard]] std::size_t FileOffset(std::uint32_t rva, std::size_t size) const;
        [[nodiscard]] std::string CString(std::uint32_t rva) const;
        [[nodiscard]] IMAGE_DATA_DIRECTORY Directory(std::size_t index) const;

        std::vector<std::byte> _file;
        std::vector<IMAGE_SECTION_HEADER> _sections;
        IMAGE_OPTIONAL_HEADER64 _optional{};
    };
}
