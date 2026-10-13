// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "host/settings.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace sp2::host
{
    namespace
    {
        // Freezes target and every object and array in it; shape is the JSON target was parsed from, so no script code runs to enumerate members.
        void FreezeAll(const js::Value& target, const core::Json& shape)
        {
            if (shape.is_object()) {
                for (const auto& item : shape.items()) {
                    if (item.value().is_structured()) {
                        FreezeAll(target.Get(item.key()), item.value());
                    }
                }
            } else if (shape.is_array()) {
                for (std::size_t i = 0; i < shape.size(); ++i) {
                    if (shape[i].is_structured()) {
                        FreezeAll(target.Get(static_cast<std::uint32_t>(i)), shape[i]);
                    }
                }
            } else {
                return;
            }
            target.Freeze();
        }
    }

    core::Json MergeSettings(core::Json defaults, const core::Json& overrides)
    {
        if (!defaults.is_object() || !overrides.is_object()) {
            return overrides;
        }
        for (const auto& item : overrides.items()) {
            if (const auto found = defaults.find(item.key()); found != defaults.end()) {
                *found = MergeSettings(std::move(*found), item.value());
            } else {
                defaults[item.key()] = item.value();
            }
        }
        return defaults;
    }

    js::Value FrozenValue(js::Runtime& runtime, const core::Json& value)
    {
        // JSON.parse defines members as own data properties, so a member called __proto__ stays an ordinary member.
        js::Value result = runtime.ParseJson(value.dump());
        FreezeAll(result, value);
        return result;
    }
}
