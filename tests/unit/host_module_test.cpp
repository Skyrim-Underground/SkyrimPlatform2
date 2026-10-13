// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "host_test_util.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <format>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using sp2::test::HostFixture;
using Lines = std::vector<std::string>;

TEST_CASE("a package imports skyrimPlatform2 and nothing else", "[host]")
{
    SECTION("the host module")
    {
        HostFixture fixture;
        fixture.Load(R"(
            import { log } from "skyrimPlatform2";
            import * as sp from "skyrimPlatform2";
            log(`exports: ${Object.keys(sp).join(", ")}`);
        )");
        CHECK(fixture.Lines() == Lines{ "[info] [test] exports: log, on, once, settings" });
    }
    SECTION("any other specifier")
    {
        const std::string specifier = GENERATE("fs", "node:fs", "./util.js", "../hello/main.js", "skyrimplatform2", "skyrimPlatform", "https://example.invalid/x.js");
        INFO("specifier: " << specifier);
        HostFixture fixture;
        CHECK_FALSE(fixture.Host().Load("main.js", std::format("import {{ log }} from \"skyrimPlatform2\";\nimport \"{}\";\nlog(\"body ran\");", specifier)));
        CHECK(fixture.Lines() == Lines{ std::format("[error] [test] main.js failed to load: TypeError: cannot import '{}': a package imports only 'skyrimPlatform2'", specifier) });
    }
    SECTION("a name the host module does not export")
    {
        HostFixture fixture;
        CHECK_FALSE(fixture.Host().Load("main.js", "import { printConsole } from \"skyrimPlatform2\";"));
        const Lines lines = fixture.Lines();
        REQUIRE(lines.size() == 1);
        CHECK_THAT(lines[0], StartsWith("[error] [test] main.js failed to load: SyntaxError: ") && ContainsSubstring("printConsole"));
    }
}

TEST_CASE("dynamic import() is refused, the host module included", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(
        import { log, once } from "skyrimPlatform2";
        const attempt = (specifier) => import(specifier).then(
            () => log(`${specifier}: imported`),
            (e) => log(`${specifier}: ${e.name}: ${e.message}`));
        for (const specifier of ["skyrimPlatform2", "fs", "./main.js"]) attempt(specifier);
        once("ready", () => attempt("skyrimPlatform2"));
    )");
    CHECK(fixture.Lines() == Lines{
        "[info] [test] skyrimPlatform2: TypeError: cannot import 'skyrimPlatform2': dynamic import() is not available",
        "[info] [test] fs: TypeError: cannot import 'fs': dynamic import() is not available",
        "[info] [test] ./main.js: TypeError: cannot import './main.js': dynamic import() is not available",
    });

    fixture.Frame();
    CHECK(fixture.Lines() == Lines{ "[info] [test] skyrimPlatform2: TypeError: cannot import 'skyrimPlatform2': dynamic import() is not available" });
}

TEST_CASE("an unhandled rejection is logged with its stack", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(function rejectWith(message) {
    return Promise.reject(new Error(message));
}
async function failLater() {
    await null;
    throw new TypeError("async failure");
}
rejectWith("nobody handles this");
const late = rejectWith("handled in the same checkpoint");
queueMicrotask(() => late.catch(() => {}));
Promise.reject("not an error");
setTimeout(() => { failLater(); }, 0);
)");

    const Lines atLoad = fixture.Lines();
    REQUIRE(atLoad.size() == 2);
    CHECK_THAT(atLoad[0], StartsWith("[error] [test] unhandled promise rejection: Error: nobody handles this\n"));
    CHECK_THAT(atLoad[0], ContainsSubstring("at rejectWith (main.js:2") && ContainsSubstring("main.js:8"));
    CHECK(atLoad[1] == "[error] [test] unhandled promise rejection: not an error");

    fixture.Frame();
    const Lines inFrame = fixture.Lines();
    REQUIRE(inFrame.size() == 1);
    CHECK_THAT(inFrame[0], StartsWith("[error] [test] unhandled promise rejection: TypeError: async failure\n"));
    CHECK_THAT(inFrame[0], ContainsSubstring("at failLater (main.js:6"));
}

TEST_CASE("console and log write to the log at their levels", "[host]")
{
    HostFixture fixture;
    fixture.Load(R"(import { log } from "skyrimPlatform2";
console.log("log", 1, 1.5, true, null, undefined, -0);
console.info({ a: [1, "b"], nested: { c: null } }, [2]);
console.warn("warn");
console.error(new RangeError("bad"));
console.debug(Symbol("s"), Symbol(), () => 0, function named() {}, 10n);
const cyclic = {};
cyclic.self = cyclic;
log("from the host module", cyclic, { toJSON() { throw new Error("no JSON"); } });
)");

    const Lines lines = fixture.Lines();
    REQUIRE(lines.size() == 6);
    CHECK(lines[0] == "[info] [test] log 1 1.5 true null undefined 0");
    CHECK(lines[1] == R"([info] [test] {"a":[1,"b"],"nested":{"c":null}} [2])");
    CHECK(lines[2] == "[warning] [test] warn");
    CHECK_THAT(lines[3], StartsWith("[error] [test] RangeError: bad\n") && ContainsSubstring("main.js:5"));
    CHECK(lines[4] == "[debug] [test] Symbol(s) Symbol() [Function (anonymous)] [Function: named] 10");
    CHECK(lines[5] == "[info] [test] from the host module [object Object] [object Object]");
}

TEST_CASE("the host adds console, timers and performance to the engine's globals", "[host]")
{
    // The engine's list is pinned in js_codegen_test.cpp; the host adds these six on top.
    constexpr std::string_view kGlobals =
        "AggregateError Array ArrayBuffer AsyncDisposableStack Atomics BigInt BigInt64Array BigUint64Array Boolean DataView "
        "Date DisposableStack Error EvalError FinalizationRegistry Float16Array Float32Array Float64Array Function Infinity "
        "Int16Array Int32Array Int8Array InternalError Iterator JSON Map Math NaN Number Object Promise Proxy RangeError "
        "ReferenceError Reflect RegExp Set SharedArrayBuffer String SuppressedError Symbol SyntaxError TypeError URIError "
        "Uint16Array Uint32Array Uint8Array Uint8ClampedArray WeakMap WeakRef WeakSet clearInterval clearTimeout console "
        "decodeURI decodeURIComponent encodeURI encodeURIComponent escape eval globalThis isFinite isNaN parseFloat parseInt "
        "performance queueMicrotask setInterval setTimeout undefined unescape";
    HostFixture fixture;
    fixture.Load(R"(
        import { log } from "skyrimPlatform2";
        log(Object.getOwnPropertyNames(globalThis).sort().join(" "));
        log(Object.keys(console).sort().join(" "));
        log(Object.keys(performance).join(" "));
    )");
    CHECK(fixture.Lines() == Lines{ std::format("[info] [test] {}", kGlobals), "[info] [test] debug error info log warn", "[info] [test] now" });
}

TEST_CASE("a module that fails to evaluate is logged with its stack and never runs again", "[host]")
{
    HostFixture fixture;
    CHECK_FALSE(fixture.Host().Load("main.js", R"(import { on } from "skyrimPlatform2";
on("tick", () => console.log("tick listener ran"));
setTimeout(() => console.log("timer ran"), 0);
Promise.reject(new Error("never reported"));
throw new Error("load failed");
)"));
    const Lines lines = fixture.Lines();
    REQUIRE(lines.size() == 1);
    CHECK_THAT(lines[0], StartsWith("[error] [test] main.js failed to load: Error: load failed\n") && ContainsSubstring("main.js:5"));

    fixture.Frame();
    fixture.Frame(16);
    CHECK(fixture.Lines().empty());
}
