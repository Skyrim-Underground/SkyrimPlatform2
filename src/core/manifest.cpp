// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/manifest.h"

#include "core/package_files.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <utility>

namespace sp2::core
{
    namespace
    {
        constexpr std::array<std::string_view, 6> kFields{ "name", "version", "sp2Api", "main", "settings", "memoryLimitMiB" };

        // A value as messages show it: scalars as JSON, strings quoted and cut short, arrays and objects by kind only.
        std::string Shown(const Json& value)
        {
            switch (value.type()) {
            case Json::value_t::string:
                return Quote(value.get_ref<const std::string&>());
            case Json::value_t::array:
                return "an array";
            case Json::value_t::object:
                return "an object";
            case Json::value_t::null:
            case Json::value_t::boolean:
            case Json::value_t::number_integer:
            case Json::value_t::number_unsigned:
            case Json::value_t::number_float:
                return value.dump();
            default:
                return "an unsupported value";
            }
        }

        // nlohmann stores every non-negative integer literal as number_unsigned.
        std::optional<std::uint64_t> NonNegativeInteger(const Json& value)
        {
            return value.is_number_unsigned() ? std::optional(value.get<std::uint64_t>()) : std::nullopt;
        }

        // Digits without a leading zero.
        bool IsNumber(std::string_view text)
        {
            return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; }) && (text.size() == 1 || text.front() != '0');
        }

        // MAJOR.MINOR.PATCH, then optionally '-' or '+' and letters, digits, '.', '-' and '+'. SemVer 2.0.0 style, not its full grammar: versions are shown, never compared.
        bool IsVersion(std::string_view text)
        {
            const auto suffix = text.find_first_of("-+");
            const auto core = text.substr(0, suffix);
            const auto number = [](const auto part) { return IsNumber(std::string_view(part.begin(), part.end())); };
            if (std::ranges::count(core, '.') != 2 || !std::ranges::all_of(std::views::split(core, '.'), number)) {
                return false;
            }
            if (suffix == std::string_view::npos) {
                return true;
            }
            const auto allowed = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '+'; };
            const auto rest = text.substr(suffix + 1);
            return !rest.empty() && std::ranges::all_of(rest, allowed);
        }
    }

    bool IsPackageName(std::string_view name)
    {
        const auto allowed = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; };
        // Windows opens these names as devices, not as folders or files.
        const bool device = name == "con" || name == "prn" || name == "aux" || name == "nul" ||
                            (name.size() == 4 && (name.starts_with("com") || name.starts_with("lpt")) && name[3] >= '0' && name[3] <= '9');
        return !name.empty() && name.size() <= 64 && std::ranges::all_of(name, allowed) && !device;
    }

    std::expected<Manifest, std::vector<std::string>> ParseManifest(std::string_view text, std::string_view folderName, std::uint32_t apiMajor)
    {
        Json root;
        try {
            root = ParseStrictJson(text);
        } catch (const JsonError& error) {
            return std::unexpected(std::vector{ std::format("invalid JSON: {}", error.what()) });
        }
        if (!root.is_object()) {
            return std::unexpected(std::vector{ std::format("expected an object at the top level, got {}", Shown(root)) });
        }

        Manifest manifest;
        std::vector<std::string> errors;
        const auto fail = [&errors](std::string_view field, std::string_view problem) { errors.push_back(std::format("{}: {}", field, problem)); };
        const auto member = [&](std::string_view field, bool required) -> const Json* {
            const auto found = root.find(std::string(field));
            if (found == root.end()) {
                if (required) {
                    fail(field, "missing");
                }
                return nullptr;
            }
            return &*found;
        };
        const auto requiredString = [&](std::string_view field) -> const std::string* {
            const Json* value = member(field, true);
            if (value != nullptr && !value->is_string()) {
                fail(field, std::format("expected a string, got {}", Shown(*value)));
                return nullptr;
            }
            return value == nullptr ? nullptr : &value->get_ref<const std::string&>();
        };

        if (const std::string* name = requiredString("name")) {
            if (!IsPackageName(*name)) {
                fail("name", std::format("{} is not a package name (1 to 64 characters of a-z, 0-9 and -, not a Windows device name such as con or com1)", Quote(*name)));
            } else if (*name != folderName) {
                fail("name", std::format("{} differs from the package's folder name {}", Quote(*name), Quote(folderName)));
            } else {
                manifest.name = *name;
            }
        }
        if (const std::string* version = requiredString("version")) {
            if (!IsVersion(*version)) {
                fail("version", std::format("{} is not a version such as 1.0.0", Quote(*version)));
            } else {
                manifest.version = *version;
            }
        }
        if (const Json* api = member("sp2Api", true)) {
            const auto major = NonNegativeInteger(*api);
            if (!major) {
                fail("sp2Api", std::format("expected a non-negative integer, got {}", Shown(*api)));
            } else if (*major != apiMajor) {
                fail("sp2Api", std::format("the package needs API {}, and this SkyrimPlatform2 provides API {}", *major, apiMajor));
            }
        }
        if (const std::string* main = requiredString("main")) {
            if (const auto problem = CheckPackagePath(*main)) {
                fail("main", std::format("{} {}", Quote(*main), *problem));
            } else if (!main->ends_with(".js")) {
                fail("main", std::format("{} is not a .js file", Quote(*main)));
            } else {
                manifest.main = *main;
            }
        }
        if (const Json* settings = member("settings", false)) {
            if (!settings->is_object()) {
                fail("settings", std::format("expected an object, got {}", Shown(*settings)));
            } else {
                manifest.settings = *settings;
            }
        }
        if (const Json* limit = member("memoryLimitMiB", false)) {
            const auto mib = NonNegativeInteger(*limit);
            if (!mib || *mib < kMinMemoryLimitMiB || *mib > kMaxMemoryLimitMiB) {
                fail("memoryLimitMiB", std::format("expected an integer from {} to {}, got {}", kMinMemoryLimitMiB, kMaxMemoryLimitMiB, Shown(*limit)));
            } else {
                manifest.memoryLimitMiB = static_cast<std::uint32_t>(*mib);
            }
        }
        for (const auto& item : root.items()) {
            if (!std::ranges::contains(kFields, item.key())) {
                errors.push_back(std::format("unknown field {}", Quote(item.key())));
            }
        }

        if (!errors.empty()) {
            return std::unexpected(std::move(errors));
        }
        return manifest;
    }
}
