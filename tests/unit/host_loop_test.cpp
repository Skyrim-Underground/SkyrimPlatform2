// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "host_test_util.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <format>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using sp2::test::HostFixture;
using Lines = std::vector<std::string>;

TEST_CASE("timers run in due order, then creation order, each followed by a microtask checkpoint", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(
        import { log } from "skyrimPlatform2";
        setTimeout(() => { log("timeout 20"); queueMicrotask(() => log("microtask of timeout 20")); }, 20);
        setTimeout(() => { log("timeout 10, first"); Promise.resolve().then(() => log("microtask of the first timeout 10")); }, 10);
        setTimeout(() => log("timeout 10, second"), 10);
        setTimeout(() => log("timeout 0"));
    )");

    fixture.Frame(30);
    CHECK(fixture.Lines() == Lines{
        "[info] [test] timeout 0",
        "[info] [test] timeout 10, first",
        "[info] [test] microtask of the first timeout 10",
        "[info] [test] timeout 10, second",
        "[info] [test] timeout 20",
        "[info] [test] microtask of timeout 20",
    });
}

TEST_CASE("a frame fires ready, then runs the inbox, the due timers and tick", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(
        import { log, on, once } from "skyrimPlatform2";
        const step = (what) => { log(what); queueMicrotask(() => log(`microtask of ${what}`)); };
        globalThis.fromInbox = step;
        setTimeout(() => step("timer"), 0);
        on("tick", ({ frame }) => step(`tick ${frame}`));
        once("ready", () => step("ready"));
    )");
    for (const char* text : { "inbox 1", "inbox 2" }) {
        fixture.Host().GetInbox().Post([text](sp2::js::Runtime& runtime) {
            const sp2::js::Value argument = runtime.String(text);
            (void)runtime.Global().Get("fromInbox").Call(sp2::js::Value{}, { &argument, 1 });
        });
    }

    fixture.Frame();
    CHECK(fixture.Lines() == Lines{
        "[info] [test] ready",
        "[info] [test] microtask of ready",
        "[info] [test] inbox 1",
        "[info] [test] microtask of inbox 1",
        "[info] [test] inbox 2",
        "[info] [test] microtask of inbox 2",
        "[info] [test] timer",
        "[info] [test] microtask of timer",
        "[info] [test] tick 1",
        "[info] [test] microtask of tick 1",
    });

    fixture.Frame(16);
    CHECK(fixture.Lines() == Lines{ "[info] [test] tick 2", "[info] [test] microtask of tick 2" });
}

TEST_CASE("inbox tasks posted from other threads run in order at the next frame", "[host]")
{
    HostFixture fixture;
    fixture.Load("");

    constexpr int kPerThread = 200;
    std::vector<std::string> ran;
    auto post = [&](int thread) {
        for (int i = 0; i < kPerThread; ++i) {
            fixture.Host().GetInbox().Post([&ran, thread, i](sp2::js::Runtime&) { ran.push_back(std::format("{}:{}", thread, i)); });
        }
    };
    std::thread first(post, 1);
    std::thread second(post, 2);
    first.join();
    second.join();
    CHECK(ran.empty());

    fixture.Host().GetInbox().Post([&](sp2::js::Runtime&) {
        fixture.Host().GetInbox().Post([&](sp2::js::Runtime&) { ran.push_back("posted by a task"); });
    });
    fixture.Frame(16);
    REQUIRE(ran.size() == 2 * kPerThread);
    for (const int thread : { 1, 2 }) {
        int next = 0;
        for (const std::string& entry : ran) {
            if (entry.starts_with(std::format("{}:", thread))) {
                CHECK(entry == std::format("{}:{}", thread, next++));
            }
        }
        CHECK(next == kPerThread);
    }

    fixture.Frame(16);
    REQUIRE(ran.size() == 2 * kPerThread + 1);
    CHECK(ran.back() == "posted by a task");
    CHECK(fixture.Lines().empty());
}

TEST_CASE("a timer created or re-armed while timers run waits for the next frame", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(
        import { log } from "skyrimPlatform2";
        let chained = 0;
        const chain = () => { chained += 1; log(`zero-delay timer ${chained}`); if (chained < 3) setTimeout(chain, 0); };
        setTimeout(chain, 0);
        let repeats = 0;
        const every = setInterval(() => { repeats += 1; log(`zero-delay interval ${repeats}`); if (repeats === 3) clearInterval(every); }, 0);
    )");

    fixture.Frame();
    CHECK(fixture.Lines() == Lines{ "[info] [test] zero-delay timer 1", "[info] [test] zero-delay interval 1" });
    // The interval keeps its creation order, so it now runs before the newer timeout.
    fixture.Frame();
    CHECK(fixture.Lines() == Lines{ "[info] [test] zero-delay interval 2", "[info] [test] zero-delay timer 2" });
    fixture.Frame();
    CHECK(fixture.Lines() == Lines{ "[info] [test] zero-delay interval 3", "[info] [test] zero-delay timer 3" });
    fixture.Frame();
    CHECK(fixture.Lines().empty());
}

TEST_CASE("intervals repeat until cleared, with their arguments, and both clear functions cancel any timer", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(
        import { log } from "skyrimPlatform2";
        const cancelled = setTimeout(() => log("cancelled timeout ran"), 10);
        clearTimeout(cancelled);
        const viaClearTimeout = setInterval(() => log("cancelled interval ran"), 10);
        clearTimeout(viaClearTimeout);
        for (const notAnId of [undefined, null, 12345, -1, 1.5, NaN, "1", {}]) clearInterval(notAnId);
        log(`ids ${cancelled} ${viaClearTimeout}`);
        let count = 0;
        const every25 = setInterval((a, b) => {
            count += 1;
            log(`interval ${count} at ${performance.now()} ms with ${a} ${b}`);
            if (count === 3) clearInterval(every25);
        }, 25, "x", 2);
    )");
    CHECK(fixture.Lines() == Lines{ "[info] [test] ids 1 2" });

    for (int i = 0; i < 12; ++i) {
        fixture.Frame(10);
    }
    CHECK(fixture.Lines() == Lines{
        "[info] [test] interval 1 at 30 ms with x 2",
        "[info] [test] interval 2 at 60 ms with x 2",
        "[info] [test] interval 3 at 90 ms with x 2",
    });
}

TEST_CASE("timers take functions only, and number delays where NaN and negative mean 0", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"js(
        import { log } from "skyrimPlatform2";
        const errorOf = (body) => { try { body(); return "no error"; } catch (e) { return `${e.name}: ${e.message}`; } };
        log(errorOf(() => setTimeout("log('from a string')", 0)));
        log(errorOf(() => setInterval({}, 0)));
        log(errorOf(() => setTimeout(() => {}, "10")));
        setTimeout(() => log("negative delay"), -5);
        setTimeout(() => log("NaN delay"), NaN);
        setTimeout(() => log("delay above 2^31 - 1 ms"), 1e300);
    )js");
    CHECK(fixture.Lines() == Lines{
        "[info] [test] TypeError: setTimeout: the handler must be a function; SkyrimPlatform2 does not run code created from strings",
        "[info] [test] TypeError: setInterval: the handler must be a function; SkyrimPlatform2 does not run code created from strings",
        "[info] [test] TypeError: argument 2: expected a number, got string",
    });

    fixture.Frame();
    CHECK(fixture.Lines() == Lines{ "[info] [test] negative delay", "[info] [test] NaN delay" });
    fixture.Frame(2147483646.0);
    CHECK(fixture.Lines().empty());
    fixture.Frame(1);
    CHECK(fixture.Lines() == Lines{ "[info] [test] delay above 2^31 - 1 ms" });
}

TEST_CASE("on and once take tick and ready listeners", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(
        import { log, on, once } from "skyrimPlatform2";
        const errorOf = (body) => { try { body(); return "no error"; } catch (e) { return `${e.name}: ${e.message}`; } };
        on("tick", (info) => log(`on: ${JSON.stringify(info)}, frozen ${Object.isFrozen(info)}`));
        once("tick", ({ frame }) => {
            log(`once: tick ${frame}`);
            on("ready", () => log("a ready listener added after ready ran"));
        });
        on("ready", (...args) => log(`ready, on, ${args.length} arguments`));
        once("ready", () => log("ready, once"));
        log(errorOf(() => on("update", () => {})));
        log(errorOf(() => once("tick", "not a function")));
        log(errorOf(() => on(1, () => {})));
    )");
    CHECK(fixture.Lines() == Lines{
        "[info] [test] TypeError: on: unknown event 'update'; the events are 'tick' and 'ready'",
        "[info] [test] TypeError: once: the listener must be a function",
        "[info] [test] TypeError: argument 1: expected a string, got number",
    });

    fixture.Frame();
    CHECK(fixture.Lines() == Lines{
        "[info] [test] ready, on, 0 arguments",
        "[info] [test] ready, once",
        R"([info] [test] on: {"dt":0,"frame":1,"paused":false,"inGame":false}, frozen true)",
        "[info] [test] once: tick 1",
    });

    fixture.Frame(16.5, true, true);
    CHECK(fixture.Lines() == Lines{ R"([info] [test] on: {"dt":16.5,"frame":2,"paused":true,"inGame":true}, frozen true)" });
}

TEST_CASE("an exception escaping a listener, timer or microtask is logged with its stack and the frame goes on", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(import { on } from "skyrimPlatform2";
function explode(where) {
    throw new RangeError(`boom in ${where}`);
}
on("tick", () => explode("a tick listener"));
on("tick", ({ frame }) => console.log(`the next tick listener runs in frame ${frame}`));
setTimeout(() => explode("a timer"), 0);
setTimeout(() => {
    queueMicrotask(() => explode("a microtask"));
    queueMicrotask(() => console.log("the next microtask runs"));
}, 0);
)");

    fixture.Frame();
    const Lines lines = fixture.Lines();
    REQUIRE(lines.size() == 5);
    CHECK_THAT(lines[0], StartsWith("[error] [test] uncaught exception in a timer: RangeError: boom in a timer\n"));
    CHECK_THAT(lines[0], ContainsSubstring("at explode (main.js:3") && ContainsSubstring("main.js:7"));
    CHECK_THAT(lines[1], StartsWith("[error] [test] uncaught exception in a microtask: RangeError: boom in a microtask\n"));
    CHECK_THAT(lines[1], ContainsSubstring("at explode (main.js:3") && ContainsSubstring("main.js:9"));
    CHECK(lines[2] == "[info] [test] the next microtask runs");
    CHECK_THAT(lines[3], StartsWith("[error] [test] uncaught exception in a tick listener: RangeError: boom in a tick listener\n"));
    CHECK_THAT(lines[3], ContainsSubstring("at explode (main.js:3") && ContainsSubstring("main.js:5"));
    CHECK(lines[4] == "[info] [test] the next tick listener runs in frame 1");
}

TEST_CASE("a C++ exception escaping an inbox task is logged and the remaining tasks, timers and tick still run", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(
        import { log, on } from "skyrimPlatform2";
        globalThis.step = (what) => { log(what); queueMicrotask(() => log(`microtask of ${what}`)); };
        setTimeout(() => log("timer"), 0);
        on("tick", ({ frame }) => log(`tick ${frame}`));
    )");
    const auto step = [](sp2::js::Runtime& runtime, const char* what) {
        const sp2::js::Value argument = runtime.String(what);
        (void)runtime.Global().Get("step").Call(sp2::js::Value{}, { &argument, 1 });
    };
    sp2::host::Inbox& inbox = fixture.Host().GetInbox();
    inbox.Post([&](sp2::js::Runtime& runtime) {
        step(runtime, "task 1");
        (void)runtime.Global().Get("missing").As<double>();
    });
    inbox.Post([](sp2::js::Runtime&) { throw std::runtime_error("worker failed"); });
    inbox.Post([](sp2::js::Runtime&) { throw 42; });
    inbox.Post([&](sp2::js::Runtime& runtime) { step(runtime, "task 4"); });

    fixture.Frame();
    CHECK(fixture.Lines() == Lines{
        "[info] [test] task 1",
        "[error] [test] uncaught C++ exception in an inbox task: expected a number, got undefined",
        "[info] [test] microtask of task 1",
        "[error] [test] uncaught C++ exception in an inbox task: worker failed",
        "[error] [test] uncaught C++ exception of unknown type in an inbox task",
        "[info] [test] task 4",
        "[info] [test] microtask of task 4",
        "[info] [test] timer",
        "[info] [test] tick 1",
    });

    fixture.Frame(16);
    CHECK(fixture.Lines() == Lines{ "[info] [test] tick 2" });
}

TEST_CASE("performance.now counts milliseconds on the host clock from the host's creation", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(
        import { log, on } from "skyrimPlatform2";
        log(`at load ${performance.now()}`);
        on("tick", ({ frame }) => log(`frame ${frame} at ${performance.now()}`));
    )");
    fixture.Frame(0.25);
    fixture.Frame(16.5);
    CHECK(fixture.Lines() == Lines{ "[info] [test] at load 0", "[info] [test] frame 1 at 0.25", "[info] [test] frame 2 at 16.75" });
}

TEST_CASE("ready waits for a top-level await; a rejected one stops the module", "[host]")
{
    SECTION("fulfilled")
    {
        HostFixture fixture;
        fixture.Load(R"(
            import { log, on, once } from "skyrimPlatform2";
            on("tick", ({ frame }) => log(`tick ${frame}`));
            once("ready", () => log("ready"));
            await new Promise((resolve) => setTimeout(resolve, 20));
            log("top-level await done");
        )");
        fixture.Frame();
        fixture.Frame(10);
        fixture.Frame(10);
        fixture.Frame(10);
        CHECK(fixture.Lines() == Lines{
            "[info] [test] tick 1",
            "[info] [test] tick 2",
            "[info] [test] top-level await done",
            "[info] [test] tick 3",
            "[info] [test] ready",
            "[info] [test] tick 4",
        });
    }
    SECTION("rejected")
    {
        HostFixture fixture;
        fixture.Load(R"(
            import { log, on } from "skyrimPlatform2";
            on("tick", ({ frame }) => log(`tick ${frame}`));
            await new Promise((_, reject) => setTimeout(() => reject(new Error("late failure")), 0));
        )");
        fixture.Frame();
        CHECK(fixture.Lines() == Lines{ "[info] [test] tick 1" });
        fixture.Frame(10);
        fixture.Frame(10);
        const Lines lines = fixture.Lines();
        REQUIRE(lines.size() == 1);
        CHECK_THAT(lines[0], StartsWith("[error] [test] main.js failed to load: Error: late failure\n"));
    }
}
