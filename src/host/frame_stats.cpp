// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "host/frame_stats.h"

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace sp2::host
{
    void FrameStats::Record(js::Clock::duration time)
    {
        times_.push_back(time);
    }

    FrameSummary FrameStats::Take()
    {
        if (times_.empty()) {
            return {};
        }
        std::ranges::sort(times_);
        const auto ms = [](js::Clock::duration time) { return std::chrono::duration<double, std::milli>(time).count(); };
        // The time at rank ceil(percent / 100 x frames), counting from 1.
        const auto percentile = [this, &ms](std::size_t percent) { return ms(times_[(percent * times_.size() + 99) / 100 - 1]); };
        const FrameSummary summary{
            .frames = times_.size(),
            .p50Ms = percentile(50),
            .p95Ms = percentile(95),
            .p99Ms = percentile(99),
            .maxMs = ms(times_.back()),
        };
        // Keeps the capacity for the next window.
        times_.clear();
        return summary;
    }
}
