// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "package_test_util.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::StartsWith;
using sp2::test::PackageFixture;
using Lines = std::vector<std::string>;

namespace
{
    // A module that logs when it loads and on every tick.
    std::string Ticker(std::string_view text = "")
    {
        return std::format(R"js(
            import {{ log, on }} from "skyrimPlatform2";
            log("loaded");
            {}
            on("tick", ({{ frame }}) => log(`tick ${{frame}}`));
        )js", text);
    }

    std::vector<bool> Running(sp2::host::PackageHost& host)
    {
        std::vector<bool> running;
        for (const auto& package : host.TakeReport()) {
            running.push_back(package.running);
        }
        return running;
    }
}

TEST_CASE("packages load and run in name order, each in its own runtime", "[packages]")
{
    PackageFixture fixture("order");
    fixture.AddPackage("zeta", Ticker("log(`marker from another package: ${typeof globalThis.marker}`);"));
    fixture.AddPackage("alpha", Ticker("globalThis.marker = 'alpha';"));
    fixture.AddPackage("mid-1", Ticker());
    fixture.Start();
    CHECK(fixture.Lines() == Lines{
        "[info] packages: alpha 1.0.0 found",
        "[info] packages: mid-1 1.0.0 found",
        "[info] packages: zeta 1.0.0 found",
        "[info] [alpha] loaded",
        "[info] packages: alpha loaded",
        "[info] [mid-1] loaded",
        "[info] packages: mid-1 loaded",
        "[info] [zeta] loaded",
        "[info] [zeta] marker from another package: undefined",
        "[info] packages: zeta loaded",
    });

    fixture.Frame();
    fixture.Frame(16);
    CHECK(fixture.Lines() == Lines{
        "[info] [alpha] tick 1",
        "[info] [mid-1] tick 1",
        "[info] [zeta] tick 1",
        "[info] [alpha] tick 2",
        "[info] [mid-1] tick 2",
        "[info] [zeta] tick 2",
    });

    const auto report = fixture.Host().TakeReport();
    REQUIRE(report.size() == 3);
    CHECK(report[0].name == "alpha");
    CHECK(report[0].version == "1.0.0");
    CHECK(report[2].name == "zeta");
    CHECK(report[2].running);
    CHECK(report[2].frames.frames == 2);
}

TEST_CASE("folders that are not packages are reported and the others still load", "[packages]")
{
    PackageFixture fixture("folders");
    fixture.AddPackage("ok", Ticker());
    fixture.Work().Write("Packages/Bad Name/sp2.json", sp2::test::Manifest("bad-name"));
    fixture.Work().Write("Packages/empty/readme.txt", "no manifest here");
    fixture.Work().Write("Packages/notes.txt", "a file, not a package folder");
    fixture.Start();
    CHECK(fixture.Lines() == Lines{
        R"([error] packages: "Bad Name" disabled: its folder name is not a package name (1 to 64 characters of a-z, 0-9 and -, not a Windows device name such as con or com1))",
        "[error] packages: empty disabled: no sp2.json",
        "[info] packages: ok 1.0.0 found",
        "[info] [ok] loaded",
        "[info] packages: ok loaded",
    });
    fixture.Frame();
    CHECK(fixture.Lines() == Lines{ "[info] [ok] tick 1" });
    CHECK(Running(fixture.Host()) == std::vector{ false, false, true });
}

TEST_CASE("a missing or empty packages folder means no packages", "[packages]")
{
    PackageFixture missing("missing");
    missing.Start();
    CHECK(missing.Lines() == Lines{ "[info] packages: none found" });

    PackageFixture empty("empty");
    std::filesystem::create_directories(empty.Work() / "Packages");
    empty.Start();
    empty.Frame();
    CHECK(empty.Lines() == Lines{ "[info] packages: none found" });
    CHECK(empty.Host().TakeReport().empty());
}

TEST_CASE("a refused manifest disables its package and the others keep running", "[packages]")
{
    PackageFixture fixture("manifests");
    fixture.AddPackage("duplicate", Ticker(), R"("sp2Api": 1)");
    fixture.Work().Write("Packages/future/sp2.json", R"({ "name": "future", "version": "2.0.0", "sp2Api": 1, "main": "main.js" })");
    fixture.Work().Write("Packages/future/main.js", Ticker());
    fixture.Work().Write("Packages/hashed/sp2.json", R"({ "name": "hashed", "version": "1.0", "sp2Api": 0, "main": "main.js", "files": {} })");
    fixture.Work().Write("Packages/hashed/main.js", Ticker());
    fixture.Work().Write("Packages/mismatch/sp2.json", sp2::test::Manifest("other"));
    fixture.AddPackage("works", Ticker());
    fixture.Start();
    CHECK(fixture.Lines() == Lines{
        R"([error] packages: duplicate disabled: sp2.json: invalid JSON: the name "sp2Api" appears twice in one object)",
        "[error] packages: future disabled: sp2.json: sp2Api: the package needs API 1, and this SkyrimPlatform2 provides API 0",
        R"([error] packages: hashed disabled: sp2.json: version: "1.0" is not a version such as 1.0.0; unknown field "files")",
        R"([error] packages: mismatch disabled: sp2.json: name: "other" differs from the package's folder name "mismatch")",
        "[info] packages: works 1.0.0 found",
        "[info] [works] loaded",
        "[info] packages: works loaded",
    });
    fixture.Frame();
    CHECK(fixture.Lines() == Lines{ "[info] [works] tick 1" });
}

TEST_CASE("a package whose module fails to load is disabled and the others keep running", "[packages]")
{
    PackageFixture fixture("load-failures");
    fixture.AddPackage("a-throws", "throw new Error('load failed');");
    fixture.AddPackage("d-late", Ticker("await new Promise((_, reject) => setTimeout(() => reject(new Error('late failure')), 30));"));
    fixture.AddPackage("e-steady", Ticker());
    fixture.Start();
    const Lines start = fixture.Lines();
    REQUIRE(start.size() == 9);
    CHECK_THAT(start[3], StartsWith("[error] [a-throws] main.js failed to load: Error: load failed\n"));
    CHECK(start[4] == "[error] packages: a-throws disabled: its module failed to load");
    CHECK(start[5] == "[info] [d-late] loaded");
    CHECK(start[6] == "[info] packages: d-late loaded");
    CHECK(start[8] == "[info] packages: e-steady loaded");

    // d-late's top-level await rejects in the frame at 30 ms, and the next frame sees it.
    for (int frame = 0; frame < 4; ++frame) {
        fixture.Frame(frame == 0 ? 0 : 10);
    }
    CHECK(fixture.Lines() == Lines{ "[info] [e-steady] tick 1", "[info] [e-steady] tick 2", "[info] [e-steady] tick 3", "[info] [e-steady] tick 4" });
    fixture.Frame(10);
    const Lines late = fixture.Lines();
    REQUIRE(late.size() == 3);
    CHECK_THAT(late[0], StartsWith("[error] [d-late] main.js failed to load: Error: late failure\n"));
    CHECK(late[1] == "[error] packages: d-late disabled: its module failed to load");
    CHECK(late[2] == "[info] [e-steady] tick 5");
    CHECK(Running(fixture.Host()) == std::vector{ false, false, true });
}

TEST_CASE("a package whose frame step fails is disabled and the others keep running", "[packages]")
{
    PackageFixture fixture("frame-failure");
    // The host assigns the tick argument's members outside any listener, so this inherited setter makes the frame step itself throw.
    fixture.AddPackage("saboteur", R"js(
        import { on } from "skyrimPlatform2";
        on("tick", () => {});
        Object.defineProperty(Object.prototype, "dt", { set() { throw new Error("no dt"); } });
    )js");
    fixture.AddPackage("steady", Ticker());
    fixture.Start();
    (void)fixture.Lines();

    fixture.Frame();
    CHECK(fixture.Lines() == Lines{ "[error] packages: saboteur disabled: its frame step failed: Error: no dt", "[info] [steady] tick 1" });
    fixture.Frame(16);
    CHECK(fixture.Lines() == Lines{ "[info] [steady] tick 2" });
    const auto report = fixture.Host().TakeReport();
    CHECK_FALSE(report[0].running);
    CHECK(report[0].frames.frames == 1);
    CHECK(report[1].frames.frames == 2);
}

TEST_CASE("each package's memory limit is its own", "[packages]")
{
    PackageFixture fixture("memory");
    const std::string allocate = R"js(
        import { log } from "skyrimPlatform2";
        try {
            globalThis.kept = new ArrayBuffer(24 * 1024 * 1024);
            log("allocated 24 MiB");
        } catch (e) {
            log(`${e.name}: ${e.message}`);
        }
    )js";
    fixture.AddPackage("large", allocate, R"("memoryLimitMiB": 64)");
    fixture.AddPackage("small", allocate, R"("memoryLimitMiB": 16)");
    fixture.Start();
    CHECK(fixture.Lines() == Lines{
        "[info] packages: large 1.0.0 found",
        "[info] packages: small 1.0.0 found",
        "[info] [large] allocated 24 MiB",
        "[info] packages: large loaded",
        "[info] [small] InternalError: out of memory",
        "[info] packages: small loaded",
    });
}

TEST_CASE("settings are the manifest's defaults merged with the package's settings file", "[packages]")
{
    PackageFixture fixture("settings");
    const std::string show = R"js(
        import { log, settings } from "skyrimPlatform2";
        log(JSON.stringify(settings), Object.isFrozen(settings.server ?? settings));
    )js";
    const std::string_view defaults = R"("settings": { "server": { "host": "127.0.0.1", "port": 7777 }, "profileId": 1 })";
    fixture.AddPackage("a-defaults", show, defaults);
    fixture.AddPackage("b-merged", show, defaults);
    fixture.Work().Write("Settings/b-merged.json", "\xEF\xBB\xBF{ \"server\": { \"host\": \"203.0.113.5\" }, \"name\": \"Lydia\" }");
    fixture.AddPackage("c-only-file", show);
    fixture.Work().Write("Settings/c-only-file.json", R"({ "profileId": 2 })");
    fixture.AddPackage("d-broken", show, defaults);
    fixture.Work().Write("Settings/d-broken.json", R"({ "server": { "port": 1, "port": 2 } })");
    fixture.AddPackage("e-array", show, defaults);
    fixture.Work().Write("Settings/e-array.json", "[1]");
    fixture.AddPackage("f-folder", show);
    std::filesystem::create_directories(fixture.Work() / "Settings/f-folder.json");
    fixture.Start();
    CHECK(fixture.Lines() == Lines{
        "[info] packages: a-defaults 1.0.0 found",
        "[info] packages: b-merged 1.0.0 found",
        "[info] packages: c-only-file 1.0.0 found",
        R"([error] packages: d-broken disabled: settings file d-broken.json: invalid JSON: the name "port" appears twice in one object)",
        "[error] packages: e-array disabled: settings file e-array.json: expected an object at the top level",
        R"([error] packages: f-folder disabled: settings file f-folder.json: "f-folder.json" is a folder)",
        R"([info] [a-defaults] {"server":{"host":"127.0.0.1","port":7777},"profileId":1} true)",
        "[info] packages: a-defaults loaded",
        R"([info] [b-merged] {"server":{"host":"203.0.113.5","port":7777},"profileId":1,"name":"Lydia"} true)",
        "[info] packages: b-merged loaded",
        R"([info] [c-only-file] {"profileId":2} true)",
        "[info] packages: c-only-file loaded",
    });
}

TEST_CASE("a package whose main file is missing is disabled", "[packages]")
{
    PackageFixture fixture("confinement");
    fixture.AddPackage("d-missing", "");
    fixture.Start();
    CHECK(fixture.Lines() == Lines{
        R"([error] packages: d-missing disabled: main: "main.js" does not exist)",
    });
}

TEST_CASE("frame stats time each package's frames until it is disabled", "[packages]")
{
    PackageFixture fixture("stats");
    fixture.AddPackage("busy", R"js(
        import { on } from "skyrimPlatform2";
        let total = 0;
        on("tick", () => { for (let i = 0; i < 200000; i++) total += i % 7; });
    )js");
    fixture.AddPackage("idle", Ticker());
    fixture.AddPackage("quitter", Ticker("await new Promise((_, reject) => setTimeout(() => reject(new Error('quits')), 20));"));
    fixture.Start();
    for (int frame = 0; frame < 5; ++frame) {
        fixture.Frame(frame == 0 ? 0 : 10);
    }

    const auto report = fixture.Host().TakeReport();
    REQUIRE(report.size() == 3);
    CHECK(report[0].frames.frames == 5);
    CHECK(report[1].frames.frames == 5);
    CHECK(report[2].frames.frames == 4);
    CHECK_FALSE(report[2].running);
    CHECK(report[0].frames.p50Ms > report[1].frames.p50Ms);

    fixture.Frame(10);
    const auto next = fixture.Host().TakeReport();
    CHECK(next[0].frames.frames == 1);
    CHECK(next[2].frames.frames == 0);
    CHECK(next[2].frames.maxMs == 0);
}
