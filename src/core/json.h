// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

namespace sp2::core
{
	using Json = nlohmann::ordered_json;

	// Nesting limit, so a hostile file cannot exhaust the stack when the result is walked.
	inline constexpr int kMaxJsonDepth = 64;

	// Strict RFC 8259 (BOM allowed): no comments, trailing commas, duplicate names or nesting past kMaxJsonDepth.
	[[nodiscard]] Json ParseStrictJson(std::string_view text);

	// text as a JSON string literal for one log line: control characters escaped, invalid UTF-8 replaced, cut at 64 bytes with "..." after the closing quote.
	[[nodiscard]] std::string Quote(std::string_view text);
}
