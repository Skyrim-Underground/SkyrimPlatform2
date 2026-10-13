// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "core/version.h"
#include "sp2/build_info.h"

#include <optional>
#include <span>
#include <string>

namespace sp2::plugin
{
    inline constexpr core::Version kMinimumSkse{ 2, 3, 1, 0 };

    // True when SP2_GATE_TEST_ALLOWLIST replaced the compiled allowlist (dev builds only).
    inline constexpr bool kGateTestBuild = !build::kGateTestAllowlist.empty();

    // Game versions this build starts on: {1.7.104.0}, or SP2_GATE_TEST_ALLOWLIST in a gate test build.
    [[nodiscard]] std::span<const core::Version> CompiledAllowlist() noexcept;

    // Nothing when exe is exactly one of allowlist and skse is at least kMinimumSkse; otherwise the player's refusal text, naming every failed check.
    [[nodiscard]] std::optional<std::string> Refusal(const std::optional<core::Version>& exe, const core::Version& skse, std::span<const core::Version> allowlist);

    // "1.7.104.0, 1.7.99.0", or "none" for an empty list.
    [[nodiscard]] std::string JoinVersions(std::span<const core::Version> versions);
}
