// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "core/json.h"
#include "host/js/js.h"

namespace sp2::host
{
    // overrides deep-merged into defaults
    [[nodiscard]] core::Json MergeSettings(core::Json defaults, const core::Json& overrides);

    // value as a script value built by the engine's JSON parser and frozen at every level. value nests at most core::kMaxJsonDepth levels.
    [[nodiscard]] js::Value FrozenValue(js::Runtime& runtime, const core::Json& value);
}
