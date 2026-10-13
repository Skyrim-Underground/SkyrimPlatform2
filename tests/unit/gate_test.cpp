// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "plugin/gate.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using sp2::core::Version;

    struct Row
    {
        std::string_view name;
        std::optional<Version> exe;
        Version skse;
        std::vector<Version> allowlist;
        bool start;
        // Refuse rows only: phrases the text must contain and must not contain.
        std::vector<std::string_view> mentions;
        std::vector<std::string_view> omits;
    };

    const std::vector<Version> kShipped{ { 1, 7, 104, 0 } };
    const std::vector<Version> kRefusalTest{ { 1, 7, 99, 0 } };

    const std::vector<Row> kRows{
        { "supported game, minimum SKSE", Version{ 1, 7, 104, 0 }, { 2, 3, 1, 0 }, kShipped, true, {}, {} },
        { "supported game, newer SKSE build", Version{ 1, 7, 104, 0 }, { 2, 3, 2, 0 }, kShipped, true, {}, {} },
        { "supported game, newer SKSE minor", Version{ 1, 7, 104, 0 }, { 2, 4, 0, 0 }, kShipped, true, {}, {} },
        { "supported game, newer SKSE major", Version{ 1, 7, 104, 0 }, { 3, 0, 0, 0 }, kShipped, true, {}, {} },
        { "SKSE 2.3.0, one build too old", Version{ 1, 7, 104, 0 }, { 2, 3, 0, 0 }, kShipped, false, { "SKSE 2.3.0.0 is too old", "2.3.1.0 or later" }, { "Game version" } },
        { "SKSE 2.2.6 for runtime 1.6.1170", Version{ 1, 7, 104, 0 }, { 2, 2, 6, 0 }, kShipped, false, { "SKSE 2.2.6.0 is too old" }, { "Game version" } },
        { "previous runtime 1.7.99", Version{ 1, 7, 99, 0 }, { 2, 3, 1, 0 }, kShipped, false, { "Game version 1.7.99.0 is not supported", "supported: 1.7.104.0" }, { "SKSE" } },
        { "runtime 1.6.1170", Version{ 1, 6, 1170, 0 }, { 2, 3, 1, 0 }, kShipped, false, { "Game version 1.6.1170.0" }, { "SKSE" } },
        { "newer runtime 1.7.105 is not assumed compatible", Version{ 1, 7, 105, 0 }, { 2, 3, 1, 0 }, kShipped, false, { "Game version 1.7.105.0" }, {} },
        { "fourth field differs (GOG-style 1.7.104.1)", Version{ 1, 7, 104, 1 }, { 2, 3, 1, 0 }, kShipped, false, { "Game version 1.7.104.1" }, {} },
        { "game version could not be read", std::nullopt, { 2, 3, 1, 0 }, kShipped, false, { "could not be read", "supported: 1.7.104.0" }, { "SKSE" } },
        { "game and SKSE both wrong", Version{ 1, 7, 99, 0 }, { 2, 3, 0, 0 }, kShipped, false, { "Game version 1.7.99.0", "SKSE 2.3.0.0 is too old" }, {} },
        { "refusal-test allowlist refuses 1.7.104", Version{ 1, 7, 104, 0 }, { 2, 3, 1, 0 }, kRefusalTest, false, { "Game version 1.7.104.0", "supported: 1.7.99.0" }, { "SKSE" } },
        { "refusal-test allowlist starts on 1.7.99", Version{ 1, 7, 99, 0 }, { 2, 3, 1, 0 }, kRefusalTest, true, {}, {} },
        { "two-entry allowlist, second entry", Version{ 1, 7, 104, 0 }, { 2, 3, 1, 0 }, { { 1, 7, 99, 0 }, { 1, 7, 104, 0 } }, true, {}, {} },
        { "two-entry allowlist lists both", Version{ 1, 6, 1170, 0 }, { 2, 3, 1, 0 }, { { 1, 7, 99, 0 }, { 1, 7, 104, 0 } }, false, { "supported: 1.7.99.0, 1.7.104.0" }, {} },
        { "empty allowlist refuses everything", Version{ 1, 7, 104, 0 }, { 2, 3, 1, 0 }, {}, false, { "supported: none" }, {} },
    };
}

TEST_CASE("gate starts or refuses each row of the table", "[gate]")
{
    const auto& row = GENERATE(Catch::Generators::from_range(kRows));
    INFO("row: " << row.name);

    const auto refusal = sp2::plugin::Refusal(row.exe, row.skse, row.allowlist);

    if (row.start) {
        CHECK_FALSE(refusal.has_value());
        return;
    }
    REQUIRE(refusal.has_value());
    INFO("text: " << *refusal);
    CHECK(refusal->starts_with(std::string(sp2::build::kProductName) + " did not start."));
    for (const auto mention : row.mentions) {
        CHECK(refusal->find(mention) != std::string::npos);
    }
    for (const auto omission : row.omits) {
        CHECK(refusal->find(omission) == std::string::npos);
    }
}

TEST_CASE("compiled allowlist is exactly 1.7.104.0 unless this is a gate test build", "[gate]")
{
    const auto allowlist = sp2::plugin::CompiledAllowlist();

    if constexpr (sp2::plugin::kGateTestBuild) {
        CHECK(std::ranges::equal(allowlist, sp2::build::kGateTestAllowlist));
    } else {
        CHECK(std::ranges::equal(allowlist, std::vector<Version>{ { 1, 7, 104, 0 } }));
    }
}

TEST_CASE("version lists are joined for the log and the refusal text", "[gate]")
{
    CHECK(sp2::plugin::JoinVersions(std::vector<Version>{}) == "none");
    CHECK(sp2::plugin::JoinVersions(kShipped) == "1.7.104.0");
    CHECK(sp2::plugin::JoinVersions(std::vector<Version>{ { 1, 7, 99, 0 }, { 1, 7, 104, 0 } }) == "1.7.99.0, 1.7.104.0");
}
