// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "pe_image.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>

namespace sp2::test
{
    namespace
    {
        template <class T>
        constexpr std::uint32_t kSize = static_cast<std::uint32_t>(sizeof(T));
    }

    PeImage::PeImage(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("cannot open " + path.string());
        }
        const std::vector<char> raw{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        _file.resize(raw.size());
        std::memcpy(_file.data(), raw.data(), raw.size());

        const auto dos = Read<IMAGE_DOS_HEADER>(0);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) {
            throw std::runtime_error("not a PE file");
        }
        const auto ntOffset = static_cast<std::size_t>(dos.e_lfanew);
        const auto nt = Read<IMAGE_NT_HEADERS64>(ntOffset);
        if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            throw std::runtime_error("not a PE32+ image");
        }
        _optional = nt.OptionalHeader;

        const auto sectionTable = ntOffset + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
        for (std::size_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            _sections.push_back(Read<IMAGE_SECTION_HEADER>(sectionTable + i * sizeof(IMAGE_SECTION_HEADER)));
        }
    }

    std::vector<std::string> PeImage::ExportNames() const
    {
        std::vector<std::string> names;
        for (auto& entry : Exports()) {
            names.push_back(std::move(entry.first));
        }
        return names;
    }

    std::optional<std::uint32_t> PeImage::ExportRva(std::string_view name) const
    {
        const auto exports = Exports();
        const auto found = std::ranges::find_if(exports, [name](const auto& entry) { return entry.first == name; });
        if (found == exports.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<std::string> PeImage::ImportedModules() const
    {
        std::vector<std::string> modules;

        if (const auto imports = Directory(IMAGE_DIRECTORY_ENTRY_IMPORT); imports.Size != 0) {
            for (auto rva = imports.VirtualAddress;; rva += kSize<IMAGE_IMPORT_DESCRIPTOR>) {
                const auto descriptor = Read<IMAGE_IMPORT_DESCRIPTOR>(FileOffset(rva, kSize<IMAGE_IMPORT_DESCRIPTOR>));
                if (descriptor.Name == 0) {
                    break;
                }
                modules.push_back(CString(descriptor.Name));
            }
        }

        if (const auto delayed = Directory(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT); delayed.Size != 0) {
            for (auto rva = delayed.VirtualAddress;; rva += kSize<IMAGE_DELAYLOAD_DESCRIPTOR>) {
                const auto descriptor = Read<IMAGE_DELAYLOAD_DESCRIPTOR>(FileOffset(rva, kSize<IMAGE_DELAYLOAD_DESCRIPTOR>));
                if (descriptor.DllNameRVA == 0) {
                    break;
                }
                if (descriptor.Attributes.RvaBased == 0) {
                    throw std::runtime_error("delay-load descriptor is not RVA based");
                }
                modules.push_back(CString(descriptor.DllNameRVA));
            }
        }
        return modules;
    }

    std::span<const std::byte> PeImage::Bytes(std::uint32_t rva, std::size_t size) const
    {
        return std::span(_file).subspan(FileOffset(rva, size), size);
    }

    template <class T>
    T PeImage::Read(std::size_t offset) const
    {
        if (offset > _file.size() || _file.size() - offset < sizeof(T)) {
            throw std::runtime_error("read past end of file");
        }
        T value{};
        std::memcpy(&value, _file.data() + offset, sizeof(T));
        return value;
    }

    std::vector<std::pair<std::string, std::uint32_t>> PeImage::Exports() const
    {
        std::vector<std::pair<std::string, std::uint32_t>> exports;
        const auto directory = Directory(IMAGE_DIRECTORY_ENTRY_EXPORT);
        if (directory.Size == 0) {
            return exports;
        }
        const auto table = Read<IMAGE_EXPORT_DIRECTORY>(FileOffset(directory.VirtualAddress, kSize<IMAGE_EXPORT_DIRECTORY>));
        for (std::uint32_t i = 0; i < table.NumberOfNames; ++i) {
            const auto nameRva = Read<DWORD>(FileOffset(table.AddressOfNames + i * kSize<DWORD>, kSize<DWORD>));
            const std::uint32_t index = Read<WORD>(FileOffset(table.AddressOfNameOrdinals + i * kSize<WORD>, kSize<WORD>));
            if (index >= table.NumberOfFunctions) {
                throw std::runtime_error("export ordinal out of range");
            }
            exports.emplace_back(CString(nameRva), Read<DWORD>(FileOffset(table.AddressOfFunctions + index * kSize<DWORD>, kSize<DWORD>)));
        }
        return exports;
    }

    std::size_t PeImage::FileOffset(std::uint32_t rva, std::size_t size) const
    {
        for (const auto& section : _sections) {
            if (rva < section.VirtualAddress) {
                continue;
            }
            const std::size_t delta = rva - section.VirtualAddress;
            if (delta < section.SizeOfRawData && section.SizeOfRawData - delta >= size) {
                const std::size_t offset = section.PointerToRawData + delta;
                if (offset > _file.size() || _file.size() - offset < size) {
                    throw std::runtime_error("section data past end of file");
                }
                return offset;
            }
        }
        throw std::runtime_error("RVA not backed by file data");
    }

    std::string PeImage::CString(std::uint32_t rva) const
    {
        const auto begin = _file.begin() + static_cast<std::ptrdiff_t>(FileOffset(rva, 1));
        const auto end = std::find(begin, _file.end(), std::byte{ 0 });
        if (end == _file.end()) {
            throw std::runtime_error("unterminated string");
        }
        std::string text(static_cast<std::size_t>(end - begin), '\0');
        std::memcpy(text.data(), std::to_address(begin), text.size());
        return text;
    }

    IMAGE_DATA_DIRECTORY PeImage::Directory(std::size_t index) const
    {
        if (index >= _optional.NumberOfRvaAndSizes || index >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES) {
            return {};
        }
        return _optional.DataDirectory[index];
    }
}
