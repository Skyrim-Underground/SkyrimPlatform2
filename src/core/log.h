// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <spdlog/logger.h>

namespace sp2::core
{
	inline constexpr std::string_view kLogFileName = "SkyrimPlatform2.log";

	struct LogOptions
	{
		std::size_t maxFileBytes = 5 * 1024 * 1024;
		// Kept as SkyrimPlatform2.1.log (newest) to SkyrimPlatform2.<olderFiles>.log.
		std::size_t olderFiles = 3;
	};

	// Product, version and build flavour. Never a path.
	[[nodiscard]] std::string StartupLine();

	// Replaces profile with %USERPROFILE%, ignoring ASCII case and slash direction.
	[[nodiscard]] std::string RedactProfile(std::string_view text, std::string_view profile);

	// Newlines become indented continuation lines and other control characters \xNN, so a message cannot forge an entry.
	[[nodiscard]] std::string ConfineToEntry(std::string_view text);

	// Each call starts a new file and rotates the old ones. Every line is flushed and has profile (UTF-8) redacted. Returns nullptr if the folder or file cannot be created.
	[[nodiscard]] std::shared_ptr<spdlog::logger> OpenLog(const std::filesystem::path& directory, const std::filesystem::path& profile, LogOptions options = {});
}
