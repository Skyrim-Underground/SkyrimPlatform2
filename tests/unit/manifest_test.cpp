// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/manifest.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using sp2::core::IsPackageName;
using sp2::core::Manifest;
using sp2::core::ParseManifest;
using Errors = std::vector<std::string>;

namespace
{
    constexpr std::string_view kValid = R"("name": "demo", "version": "1.0.0", "sp2Api": 0, "main": "main.js")";
    // The required fields with the values kValid gives them.
    constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kRequired{ {
        { "name", R"("demo")" },
        { "version", R"("1.0.0")" },
        { "sp2Api", "0" },
        { "main", R"("main.js")" },
    } };

    // The errors ParseManifest reports for an object of these members in the folder "demo".
    Errors ErrorsOf(std::string_view members, std::uint32_t apiMajor = 0)
    {
        const auto result = ParseManifest(std::format("{{ {} }}", members), "demo", apiMajor);
        if (result) {
            FAIL_CHECK("the manifest was accepted");
            return {};
        }
        return result.error();
    }

    // kValid's members with field set to value, added after them if kValid has no such field.
    std::string With(std::string_view field, std::string_view value)
    {
        std::string members;
        bool matched = false;
        for (const auto& [name, original] : kRequired) {
            matched = matched || name == field;
            members += std::format(R"({}"{}": {})", members.empty() ? "" : ", ", name, name == field ? value : original);
        }
        if (!matched) {
            members += std::format(R"(, "{}": {})", field, value);
        }
        return members;
    }
}

TEST_CASE("a minimal manifest gets the documented defaults", "[manifest]")
{
    const auto manifest = ParseManifest(std::format("{{ {} }}", kValid), "demo", 0);
    REQUIRE(manifest);
    CHECK(manifest->name == "demo");
    CHECK(manifest->version == "1.0.0");
    CHECK(manifest->main == "main.js");
    CHECK(manifest->settings.dump() == "{}");
    CHECK(manifest->memoryLimitMiB == 256);
}

TEST_CASE("a full manifest is read field by field", "[manifest]")
{
    const auto manifest = ParseManifest(R"({
        "name": "sug-client",
        "version": "1.2.3-rc.1+build.7",
        "sp2Api": 0,
        "main": "dist/main.js",
        "settings": { "server": { "port": 7777, "host": "127.0.0.1" }, "list": [1, 2] },
        "memoryLimitMiB": 64
    })", "sug-client", 0);
    REQUIRE(manifest);
    CHECK(manifest->version == "1.2.3-rc.1+build.7");
    CHECK(manifest->main == "dist/main.js");
    CHECK(manifest->settings.dump() == R"({"server":{"port":7777,"host":"127.0.0.1"},"list":[1,2]})");
    CHECK(manifest->memoryLimitMiB == 64);
}

TEST_CASE("package names are 1 to 64 characters of a-z, 0-9 and -", "[manifest]")
{
    for (const std::string_view name : { "a", "sug-client", "0", "-", "a1-b2", "abcdefghijklmnopqrstuvwxyz0123456789-abcdefghijklmnopqrstuvwxyz0",
                                         "con1", "comx", "lpt10", "nul-pack" }) {
        INFO("name: " << name);
        CHECK(IsPackageName(name));
    }
    for (const std::string_view name : { "", "Sug", "a_b", "a b", "a.b", "\xC3\xA9", "abcdefghijklmnopqrstuvwxyz0123456789-abcdefghijklmnopqrstuvwxyz01",
                                         "con", "prn", "aux", "nul", "com0", "com9", "lpt1" }) {
        INFO("name: " << name);
        CHECK_FALSE(IsPackageName(name));
    }
}

TEST_CASE("sp2Api must equal the host's API major", "[manifest]")
{
    CHECK(ErrorsOf(With("sp2Api", "1")) == Errors{ "sp2Api: the package needs API 1, and this SkyrimPlatform2 provides API 0" });
    CHECK(ErrorsOf(With("sp2Api", "18446744073709551615")) == Errors{ "sp2Api: the package needs API 18446744073709551615, and this SkyrimPlatform2 provides API 0" });
    CHECK(ErrorsOf(With("sp2Api", "0"), 1) == Errors{ "sp2Api: the package needs API 0, and this SkyrimPlatform2 provides API 1" });
    CHECK(ParseManifest(std::format("{{ {} }}", With("sp2Api", "1")), "demo", 1));
}

TEST_CASE("sp2Api must be a non-negative integer", "[manifest]")
{
    const auto [value, shown] = GENERATE(table<std::string_view, std::string_view>({
        { "-1", "-1" },
        { "0.0", "0.0" },
        { "0.5", "0.5" },
        { "1e0", "1.0" },
        { R"("0")", R"("0")" },
        { "null", "null" },
        { "true", "true" },
        { "[0]", "an array" },
        { "{}", "an object" },
    }));
    INFO("sp2Api: " << value);
    CHECK(ErrorsOf(With("sp2Api", value)) == Errors{ std::format("sp2Api: expected a non-negative integer, got {}", shown) });
}

TEST_CASE("each malformed field is named with what is wrong", "[manifest]")
{
    const auto [field, value, error] = GENERATE(table<std::string_view, std::string_view, std::string_view>({
        { "name", R"("Demo")", R"(name: "Demo" is not a package name (1 to 64 characters of a-z, 0-9 and -, not a Windows device name such as con or com1))" },
        { "name", R"("other")", R"(name: "other" differs from the package's folder name "demo")" },
        { "name", "7", "name: expected a string, got 7" },
        { "version", R"("1.0")", R"(version: "1.0" is not a version such as 1.0.0)" },
        { "version", R"("v1.0.0")", R"(version: "v1.0.0" is not a version such as 1.0.0)" },
        { "version", R"("01.0.0")", R"(version: "01.0.0" is not a version such as 1.0.0)" },
        { "version", R"("1.0.0-")", R"(version: "1.0.0-" is not a version such as 1.0.0)" },
        { "version", R"("1.0.0+")", R"(version: "1.0.0+" is not a version such as 1.0.0)" },
        { "version", R"("1.0.0-rc 1")", R"(version: "1.0.0-rc 1" is not a version such as 1.0.0)" },
        { "version", R"("1.0.0 ")", R"(version: "1.0.0 " is not a version such as 1.0.0)" },
        { "version", "1", "version: expected a string, got 1" },
        { "main", R"("../main.js")", R"(main: "../main.js" contains ".."; package paths stay inside the package folder)" },
        { "main", R"("/main.js")", R"(main: "/main.js" is an absolute path; package paths are relative to the package folder)" },
        { "main", R"("C:/mods/main.js")", R"(main: "C:/mods/main.js" is an absolute path; package paths are relative to the package folder)" },
        { "main", R"("dist\\main.js")", R"(main: "dist\\main.js" contains '\'; package paths separate folders with '/')" },
        { "main", R"("main.js:payload")", R"(main: "main.js:payload" contains ':', which Windows does not allow in file names)" },
        { "main", R"("aux.js")", R"(main: "aux.js" has the name "aux.js", which Windows reserves for a device)" },
        { "main", R"("main.ts")", R"(main: "main.ts" is not a .js file)" },
        { "main", R"("")", R"(main: "" is empty)" },
        { "main", "null", "main: expected a string, got null" },
        { "settings", "[]", "settings: expected an object, got an array" },
        { "settings", R"("verbose")", R"(settings: expected an object, got "verbose")" },
        { "memoryLimitMiB", "15", "memoryLimitMiB: expected an integer from 16 to 2048, got 15" },
        { "memoryLimitMiB", "2049", "memoryLimitMiB: expected an integer from 16 to 2048, got 2049" },
        { "memoryLimitMiB", "64.0", "memoryLimitMiB: expected an integer from 16 to 2048, got 64.0" },
        { "memoryLimitMiB", R"("64")", R"(memoryLimitMiB: expected an integer from 16 to 2048, got "64")" },
        { "memoryLimitMiB", "-64", "memoryLimitMiB: expected an integer from 16 to 2048, got -64" },
    }));
    INFO(field << ": " << value);
    CHECK(ErrorsOf(With(field, value)) == Errors{ std::string(error) });
}

TEST_CASE("valid values at the edges of each rule are accepted", "[manifest]")
{
    const auto [field, value] = GENERATE(table<std::string_view, std::string_view>({
        { "version", R"("0.0.0")" },
        { "version", R"("10.20.30")" },
        { "version", R"("1.0.0-0A.is.legal")" },
        { "version", R"("1.0.0-alpha-a.b-c-somethinglong+build.1-aef.1-its-okay")" },
        { "version", R"("1.0.0+0.build.1-rc.10000aaa-kk-0.1")" },
        { "main", R"("dist/a b/main.js")" },
        { "settings", "{}" },
        { "memoryLimitMiB", "16" },
        { "memoryLimitMiB", "2048" },
    }));
    INFO(field << ": " << value);
    CHECK(ParseManifest(std::format("{{ {} }}", With(field, value)), "demo", 0));
}

TEST_CASE("unknown fields, missing fields and malformed JSON are refused", "[manifest]")
{
    CHECK(ErrorsOf(std::format(R"({}, "files": {{ "main.js": "00" }}, "Name": "demo", "$schema": "x", "nativeModules": [])", kValid)) ==
          Errors{ R"(unknown field "files")", R"(unknown field "Name")", R"(unknown field "$schema")", R"(unknown field "nativeModules")" });
    CHECK(ErrorsOf("") == Errors{ "name: missing", "version: missing", "sp2Api: missing", "main: missing" });
    CHECK(ErrorsOf(std::format(R"({}, "name": "demo")", kValid)) == Errors{ R"(invalid JSON: the name "name" appears twice in one object)" });

    const auto notAnObject = ParseManifest("[]", "demo", 0);
    REQUIRE_FALSE(notAnObject);
    CHECK(notAnObject.error() == Errors{ "expected an object at the top level, got an array" });
    const auto broken = ParseManifest(std::format("{{ {}, }}", kValid), "demo", 0);
    REQUIRE_FALSE(broken);
    REQUIRE(broken.error().size() == 1);
    CHECK_THAT(broken.error().front(), StartsWith("invalid JSON: line 1, column 71: syntax error"));
}

TEST_CASE("every problem is reported at once, fields first, then unknown fields", "[manifest]")
{
    CHECK(ErrorsOf(R"("version": "1", "extra": 1, "name": "Demo", "sp2Api": 2, "main": "../x.js", "settings": 1, "memoryLimitMiB": 1)") == Errors{
        R"(name: "Demo" is not a package name (1 to 64 characters of a-z, 0-9 and -, not a Windows device name such as con or com1))",
        R"(version: "1" is not a version such as 1.0.0)",
        "sp2Api: the package needs API 2, and this SkyrimPlatform2 provides API 0",
        R"(main: "../x.js" contains ".."; package paths stay inside the package folder)",
        "settings: expected an object, got 1",
        "memoryLimitMiB: expected an integer from 16 to 2048, got 1",
        R"(unknown field "extra")",
    });
}

TEST_CASE("messages quote values safely and never show settings", "[manifest]")
{
    const Errors errors = ErrorsOf(std::format(R"("name": "line\nbreak{}", "version": "1.0.0", "sp2Api": 0, "main": "main.js", "settings": {{ "token": "s3cr3t" }}, "memoryLimitMiB": 1)", std::string(100, 'x')));
    REQUIRE(errors.size() == 2);
    CHECK(errors[0] == std::format(R"(name: "line\nbreak{}"... is not a package name (1 to 64 characters of a-z, 0-9 and -, not a Windows device name such as con or com1))", std::string(54, 'x')));
    for (const std::string& error : errors) {
        CHECK(error.find('\n') == std::string::npos);
        CHECK_THAT(error, !ContainsSubstring("s3cr3t"));
    }
}
