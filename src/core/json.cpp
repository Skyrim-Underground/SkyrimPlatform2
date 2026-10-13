// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/json.h"

#include <cstddef>
#include <format>
#include <string>
#include <unordered_set>
#include <vector>

namespace sp2::core
{
    namespace
    {
        constexpr std::size_t kMaxQuotedBytes = 64;

        // // Strip nlohmann's tag and prefix, and everything from "; last read: ", which echoes the input.
        std::string Reason(const nlohmann::json::exception& error)
        {
            std::string text = error.what();
            if (const auto tag = text.find("] "); text.starts_with("[json.exception.") && tag != std::string::npos) {
                text.erase(0, tag + 2);
            }
            constexpr std::string_view kParseError = "parse error at ";
            if (text.starts_with(kParseError)) {
                text.erase(0, kParseError.size());
            }
            if (const auto echo = text.find("; last read: "); echo != std::string::npos) {
                text.erase(echo);
            }
            return text;
        }
    }

    Json ParseStrictJson(std::string_view text)
    {
        // The member names seen in each object being parsed, innermost last.
        std::vector<std::unordered_set<std::string>> names;
        const Json::parser_callback_t check = [&names](int depth, Json::parse_event_t event, Json& parsed) {
            switch (event) {
            case Json::parse_event_t::object_start:
            case Json::parse_event_t::array_start:
                if (depth >= kMaxJsonDepth) {
                    throw JsonError(std::format("objects and arrays nest deeper than {} levels", kMaxJsonDepth));
                }
                if (event == Json::parse_event_t::object_start) {
                    names.emplace_back();
                }
                break;
            case Json::parse_event_t::object_end:
                names.pop_back();
                break;
            case Json::parse_event_t::key:
                // Called before nlohmann stores the member, which would silently replace an earlier one.
                if (const auto& name = parsed.get_ref<const std::string&>(); !names.back().insert(name).second) {
                    throw JsonError(std::format("the name {} appears twice in one object", Quote(name)));
                }
                break;
            default:
                break;
            }
            return true;
        };
        try {
            return Json::parse(text.begin(), text.end(), check);
        } catch (const nlohmann::json::exception& error) {
            throw JsonError(Reason(error));
        }
    }

    std::string Quote(std::string_view text)
    {
        std::size_t length = text.size();
        if (length > kMaxQuotedBytes) {
            length = kMaxQuotedBytes;
            // Never cut a UTF-8 sequence in two.
            while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
                --length;
            }
        }
        std::string quoted = Json(std::string(text.substr(0, length))).dump(-1, ' ', false, Json::error_handler_t::replace);
        if (length < text.size()) {
            quoted += "...";
        }
        return quoted;
    }
}
