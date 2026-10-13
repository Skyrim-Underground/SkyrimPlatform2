// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/package_files.h"

#include "core/json.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <ranges>
#include <system_error>

namespace sp2::core
{
    namespace
    {
        constexpr std::string_view kForbiddenCharacters = "<>:\"|?*";

        char AsciiLower(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool EqualsIgnoringCase(std::string_view a, std::string_view b)
        {
            return std::ranges::equal(a, b, [](char x, char y) { return AsciiLower(x) == AsciiLower(y); });
        }

        // CON, PRN, AUX, NUL, COM0 to COM9 and LPT0 to LPT9, the last two also with a superscript 1, 2 or 3, whatever follows the first dot.
        bool IsDeviceName(std::string_view name)
        {
            std::string_view stem = name.substr(0, name.find('.'));
            while (stem.ends_with(' ')) {
                stem.remove_suffix(1);
            }
            for (const std::string_view device : { "con", "prn", "aux", "nul" }) {
                if (EqualsIgnoringCase(stem, device)) {
                    return true;
                }
            }
            if (stem.size() < 4 || !(EqualsIgnoringCase(stem.substr(0, 3), "com") || EqualsIgnoringCase(stem.substr(0, 3), "lpt"))) {
                return false;
            }
            const std::string_view number = stem.substr(3);
            return (number.size() == 1 && number[0] >= '0' && number[0] <= '9') || number == "\xC2\xB9" || number == "\xC2\xB2" || number == "\xC2\xB3";
        }

        std::filesystem::path FromUtf8(std::string_view text)
        {
            return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }
    }

    std::optional<std::string> CheckPackagePath(std::string_view path)
    {
        if (path.empty()) {
            return "is empty";
        }
        const bool drive = path.size() >= 2 && path[1] == ':' && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
        if (path.front() == '/' || path.front() == '\\' || drive) {
            return "is an absolute path; package paths are relative to the package folder";
        }
        if (path.contains('\\')) {
            return "contains '\\'; package paths separate folders with '/'";
        }
        for (const auto part : std::views::split(path, '/')) {
            const std::string_view name(part.begin(), part.end());
            if (name.empty()) {
                return "has an empty folder or file name";
            }
            if (name == "..") {
                return "contains \"..\"; package paths stay inside the package folder";
            }
            if (name == ".") {
                return "contains \".\"";
            }
            for (const char c : name) {
                if (static_cast<unsigned char>(c) < 0x20) {
                    return std::format("contains the control character U+{:04X}", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                }
                if (kForbiddenCharacters.contains(c)) {
                    return std::format("contains '{}', which Windows does not allow in file names", c);
                }
            }
            if (name.ends_with('.') || name.ends_with(' ')) {
                return std::format("has the name {}, which ends in '.' or ' '", Quote(name));
            }
            if (IsDeviceName(name)) {
                return std::format("has the name {}, which Windows reserves for a device", Quote(name));
            }
        }
        return std::nullopt;
    }

    std::expected<std::string, ReadError> ReadFileBelow(const std::filesystem::path& root, std::string_view path)
    {
        if (const auto problem = CheckPackagePath(path)) {
            return std::unexpected(ReadError{ .message = std::format("{} {}", Quote(path), *problem) });
        }
        const auto file = root / FromUtf8(path);
        std::error_code error;
        switch (std::filesystem::status(file, error).type()) {
        case std::filesystem::file_type::not_found:
            return std::unexpected(ReadError{ .missing = true, .message = std::format("{} does not exist", Quote(path)) });
        case std::filesystem::file_type::directory:
            return std::unexpected(ReadError{ .message = std::format("{} is a folder", Quote(path)) });
        default:
            break;
        }
        std::ifstream stream(file, std::ios::binary);
        std::string text(std::istreambuf_iterator<char>(stream), {});
        if (!stream.is_open() || stream.bad()) {
            return std::unexpected(ReadError{ .message = std::format("{} cannot be read", Quote(path)) });
        }
        return text;
    }
}
