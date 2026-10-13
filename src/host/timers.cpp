// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "host/timers.h"

#include <algorithm>
#include <utility>

namespace sp2::host
{
    Timers::Id Timers::Add(js::Clock::time_point due, std::optional<js::Clock::duration> repeat, js::Value callback, std::vector<js::Value> args)
    {
        const Id id = next_++;
        timers_.emplace(id, Timer{ due, repeat, std::move(callback), std::move(args) });
        return id;
    }

    void Timers::Cancel(Id id) noexcept
    {
        timers_.erase(id);
    }

    std::vector<Timers::Id> Timers::Due(js::Clock::time_point now) const
    {
        std::vector<Id> due;
        for (const auto& [id, timer] : timers_) {
            if (timer.due <= now) {
                due.push_back(id);
            }
        }
        std::ranges::sort(due, {}, [this](Id id) { return std::pair(timers_.at(id).due, id); });
        return due;
    }

    std::optional<Timers::Fire> Timers::Take(Id id, js::Clock::time_point now)
    {
        const auto found = timers_.find(id);
        if (found == timers_.end()) {
            return std::nullopt;
        }
        Timer& timer = found->second;
        if (timer.repeat) {
            timer.due = now + *timer.repeat;
            return Fire{ timer.callback, timer.args };
        }
        Fire fire{ std::move(timer.callback), std::move(timer.args) };
        timers_.erase(found);
        return fire;
    }
}
