// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "host/frame_stats.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <vector>

using namespace std::chrono_literals;
using sp2::host::FrameStats;

TEST_CASE("frame stats report nearest-rank percentiles of the frames recorded", "[stats]")
{
    FrameStats stats;
    std::vector<int> ms(100);
    std::iota(ms.begin(), ms.end(), 1);
    std::ranges::shuffle(ms, std::mt19937(12345));
    for (const int value : ms) {
        stats.Record(std::chrono::milliseconds(value));
    }

    const auto summary = stats.Take();
    CHECK(summary.frames == 100);
    CHECK(summary.p50Ms == 50.0);
    CHECK(summary.p95Ms == 95.0);
    CHECK(summary.p99Ms == 99.0);
    CHECK(summary.maxMs == 100.0);
}

TEST_CASE("frame stats of a few frames", "[stats]")
{
    FrameStats stats;
    for (const auto time : { 4ms, 1ms, 3ms, 2ms }) {
        stats.Record(time);
    }
    const auto four = stats.Take();
    CHECK(four.frames == 4);
    CHECK(four.p50Ms == 2.0);
    CHECK(four.p95Ms == 4.0);
    CHECK(four.p99Ms == 4.0);
    CHECK(four.maxMs == 4.0);

    stats.Record(250us);
    const auto one = stats.Take();
    CHECK(one.frames == 1);
    CHECK(one.p50Ms == 0.25);
    CHECK(one.maxMs == 0.25);
}

TEST_CASE("taking the frame stats starts a new window", "[stats]")
{
    FrameStats stats;
    const auto empty = stats.Take();
    CHECK(empty.frames == 0);
    CHECK(empty.maxMs == 0.0);

    stats.Record(10ms);
    (void)stats.Take();
    CHECK(stats.Take().frames == 0);
}
