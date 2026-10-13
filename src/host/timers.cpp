// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "host/timers.h"

namespace sp2::host
{
    Timers::Id Timers::Add(js::Clock::time_point due, std::optional<js::Clock::duration> repeat, js::Value callback, std::vector<js::Value> args)
    {
        const Id id = next_++;
        const auto [timer, added] = timers_.emplace(id, Timer{ due, repeat, std::move(callback), std::move(args) });
        try {
            order_.emplace(due, id);
        } catch (...) {
            timers_.erase(timer);
            throw;
        }
        return id;
    }

    void Timers::Cancel(Id id) noexcept
    {
        const auto found = timers_.find(id);
        if (found == timers_.end()) {
            return;
        }
        order_.erase({ found->second.due, id });
        timers_.erase(found);
    }

    std::vector<Timers::Id> Timers::Due(js::Clock::time_point now) const
    {
        std::vector<Id> due;
        for (const auto& [time, id] : order_) {
            if (time > now) {
                break;
            }
            due.push_back(id);
        }
        return due;
    }

    std::optional<Timers::Fire> Timers::Take(Id id, js::Clock::time_point now)
    {
        const auto found = timers_.find(id);
        if (found == timers_.end()) {
            return std::nullopt;
        }
        Timer& timer = found->second;
        if (!timer.repeat) {
            order_.erase({ timer.due, id });
            Fire fire{ std::move(timer.callback), std::move(timer.args) };
            timers_.erase(found);
            return fire;
        }
        Fire fire{ timer.callback, timer.args };
        order_.erase({ timer.due, id });
        timer.due = now + *timer.repeat;
        try {
            order_.emplace(timer.due, id);
        } catch (...) {
            timers_.erase(found);
            throw;
        }
        return fire;
    }
}
