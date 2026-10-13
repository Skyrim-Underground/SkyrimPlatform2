// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "host/js/js.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace sp2::host
{
    // The setTimeout and setInterval timers of one runtime. Timers run in due order, then in creation order.
    class Timers
    {
    public:
        using Id = std::uint64_t;

        struct Fire
        {
            js::Value callback;
            std::vector<js::Value> args;
        };

        // Ids start at 1 and are never reused. A repeat makes an interval.
        Id Add(js::Clock::time_point due, std::optional<js::Clock::duration> repeat, js::Value callback, std::vector<js::Value> args);
        // Unknown ids are ignored.
        void Cancel(Id id) noexcept;
        // The timers due at now, in the order they run.
        [[nodiscard]] std::vector<Id> Due(js::Clock::time_point now) const;
        // Takes a due timer to run it: a timeout is removed, an interval re-armed at now plus its interval. nullopt if it was cancelled meanwhile.
        [[nodiscard]] std::optional<Fire> Take(Id id, js::Clock::time_point now);

    private:
        struct Timer
        {
            js::Clock::time_point due;
            std::optional<js::Clock::duration> repeat;
            js::Value callback;
            std::vector<js::Value> args;
        };

        std::map<Id, Timer> timers_;
        Id next_ = 1;
    };
}
