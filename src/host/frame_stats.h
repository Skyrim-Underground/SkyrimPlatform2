// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "host/js/js.h"

#include <cstdint>
#include <vector>

namespace sp2::host
{
    // JS time per frame over a window of frames, in milliseconds.
    struct FrameSummary
    {
        std::uint64_t frames = 0;
        double p50Ms = 0;
        double p95Ms = 0;
        double p99Ms = 0;
        double maxMs = 0;
    };

    // The time one package's frame steps took (decision 0005, section 2: frame order step 4, record stats).
    class FrameStats
    {
    public:
        void Record(js::Clock::duration time);

        // The frames recorded since the previous call, with nearest-rank percentiles (the smallest time that at least that share of the frames do not exceed); all zero when there were none. Starts a new window.
        [[nodiscard]] FrameSummary Take();

    private:
        std::vector<js::Clock::duration> times_;
    };
}
