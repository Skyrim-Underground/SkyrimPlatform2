// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/json.h"
#include "host/settings.h"
#include "host_test_util.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <string_view>
#include <vector>

using sp2::core::ParseStrictJson;
using sp2::host::MergeSettings;
using sp2::test::HostFixture;
using Lines = std::vector<std::string>;

TEST_CASE("settings files merge into the defaults member by member", "[settings]")
{
    const auto [defaults, overrides, merged] = GENERATE(table<std::string_view, std::string_view, std::string_view>({
        { R"({"a":1,"b":2})", R"({})", R"({"a":1,"b":2})" },
        { R"({"a":1,"b":2})", R"({"b":3})", R"({"a":1,"b":3})" },
        { R"({"a":1})", R"({"c":{"d":true},"b":null})", R"({"a":1,"c":{"d":true},"b":null})" },
        { R"({"s":{"x":1,"y":{"z":1,"w":2}}})", R"({"s":{"y":{"w":3}}})", R"({"s":{"x":1,"y":{"z":1,"w":3}}})" },
        { R"({"list":[1,2,3]})", R"({"list":[4]})", R"({"list":[4]})" },
        { R"({"o":{"x":1}})", R"({"o":5})", R"({"o":5})" },
        { R"({"o":5})", R"({"o":{"x":1}})", R"({"o":{"x":1}})" },
        { R"({"o":{"x":1}})", R"({"o":null})", R"({"o":null})" },
        { R"({"o":{"x":1}})", R"({"o":[]})", R"({"o":[]})" },
        { R"({"z":1,"a":2})", R"({"m":3,"a":4})", R"({"z":1,"a":4,"m":3})" },
        { R"({})", R"({"only":"override"})", R"({"only":"override"})" },
    }));
    INFO(defaults << " + " << overrides);
    CHECK(MergeSettings(ParseStrictJson(defaults), ParseStrictJson(overrides)).dump() == merged);
}

TEST_CASE("the host module exports the settings frozen at every level", "[settings]")
{
    HostFixture fixture({ .settings = ParseStrictJson(R"({"greeting":"hi","audience":{"name":"Tamriel","tags":["a",{"deep":1}]},"count":2})") });
    fixture.Load(R"js(
        import { log, settings } from "skyrimPlatform2";
        log(JSON.stringify(settings));
        log([settings, settings.audience, settings.audience.tags, settings.audience.tags[1]].map(Object.isFrozen).join());
        const attempts = [
            () => { settings.greeting = "changed"; },
            () => { settings.audience.name = "changed"; },
            () => { settings.audience.tags.push("b"); },
            () => { settings.audience.tags[1].deep = 2; },
            () => { delete settings.count; },
            () => { settings.added = true; },
            () => { Object.setPrototypeOf(settings, null); },
        ];
        log(attempts.map((attempt) => { try { attempt(); return "allowed"; } catch (e) { return e.name; } }).join());
        log(JSON.stringify(settings));
    )js");
    CHECK(fixture.Lines() == Lines{
        R"([info] [test] {"greeting":"hi","audience":{"name":"Tamriel","tags":["a",{"deep":1}]},"count":2})",
        "[info] [test] true,true,true,true",
        "[info] [test] TypeError,TypeError,TypeError,TypeError,TypeError,TypeError,TypeError",
        R"([info] [test] {"greeting":"hi","audience":{"name":"Tamriel","tags":["a",{"deep":1}]},"count":2})",
    });
}

TEST_CASE("a settings member called __proto__ stays an ordinary member", "[settings]")
{
    HostFixture fixture({ .settings = ParseStrictJson(R"({"__proto__":{"polluted":true},"plain":{"__proto__":null}})") });
    fixture.Load(R"js(
        import { log, settings } from "skyrimPlatform2";
        log(Object.getPrototypeOf(settings) === Object.prototype, Object.getPrototypeOf(settings.plain) === Object.prototype);
        log(Object.keys(settings).join(), settings.polluted, ({}).polluted, settings.__proto__.polluted, Object.isFrozen(settings.__proto__));
    )js");
    CHECK(fixture.Lines() == Lines{ "[info] [test] true true", "[info] [test] __proto__,plain undefined undefined true true" });
}

TEST_CASE("a package without settings gets an empty frozen object", "[settings]")
{
    HostFixture fixture;
    fixture.Load(R"js(
        import { log, settings } from "skyrimPlatform2";
        log(JSON.stringify(settings), Object.isFrozen(settings));
    )js");
    CHECK(fixture.Lines() == Lines{ "[info] [test] {} true" });
}
