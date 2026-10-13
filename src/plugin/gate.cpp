// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "plugin/gate.h"

#include <algorithm>
#include <array>
#include <format>
#include <ranges>

namespace sp2::plugin
{
    namespace
    {
        constexpr std::array<core::Version, 1> kSupportedRuntimes{ { { 1, 7, 104, 0 } } };
    }

    std::span<const core::Version> CompiledAllowlist() noexcept
    {
        if constexpr (kGateTestBuild) {
            return build::kGateTestAllowlist;
        } else {
            return kSupportedRuntimes;
        }
    }

    std::optional<std::string> Refusal(const std::optional<core::Version>& exe, const core::Version& skse, std::span<const core::Version> allowlist)
    {
        std::string reasons;
        if (!exe) {
            reasons += std::format(" The game version could not be read (supported: {}).", JoinVersions(allowlist));
        } else if (std::ranges::find(allowlist, *exe) == allowlist.end()) {
            reasons += std::format(" Game version {} is not supported (supported: {}).", core::ToString(*exe), JoinVersions(allowlist));
        }
        if (skse < kMinimumSkse) {
            reasons += std::format(" SKSE {} is too old ({} or later is required).", core::ToString(skse), core::ToString(kMinimumSkse));
        }
        if (reasons.empty()) {
            return std::nullopt;
        }
        return std::format("{} did not start.{}", build::kProductName, reasons);
    }

    std::string JoinVersions(std::span<const core::Version> versions)
    {
        if (versions.empty()) {
            return "none";
        }
        return versions | std::views::transform(core::ToString) | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>();
    }
}
