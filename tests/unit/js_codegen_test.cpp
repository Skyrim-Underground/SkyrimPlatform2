// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "js_test_util.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <string>
#include <string_view>

using Catch::Matchers::ContainsSubstring;
using sp2::js::Exception;
using sp2::js::Runtime;
using sp2::js::Value;
using sp2::test::RunModule;
using sp2::test::Thrown;

namespace
{
    struct Path
    {
        std::string_view name;
        std::string_view source;
    };

    // Every way a script could reach the parser. The lockdown is applied in every build configuration, so these run in the release presets as well as the dev ones.
    constexpr std::array kPaths{
        Path{ "direct eval", "eval('1 + 1');" },
        Path{ "indirect eval", "(0, eval)('1 + 1');" },
        Path{ "eval through globalThis", "globalThis.eval('1 + 1');" },
        Path{ "eval through a computed name", "globalThis['ev' + 'al']('1 + 1');" },
        Path{ "eval with an optional call", "eval?.('1 + 1');" },
        Path{ "new Function", "new Function('return 1');" },
        Path{ "Function without new", "Function('return 1');" },
        Path{ "an arrow function's constructor", "(() => {}).constructor('return 1');" },
        Path{ "a class's constructor", "(class {}).constructor('return 1');" },
        Path{ "a built-in function's constructor", "Math.max.constructor('return 1');" },
        Path{ "a bound function's constructor", "(function () {}).bind(null).constructor('return 1');" },
        Path{ "Function.prototype.constructor", "Function.prototype.constructor('return 1');" },
        Path{ "the constructor property descriptor", "Object.getOwnPropertyDescriptor(Function.prototype, 'constructor').value('return 1');" },
        Path{ "Reflect.construct", "Reflect.construct(Function, ['return 1']);" },
        Path{ "Function.prototype.call", "Function.prototype.call.call(Function, null, 'return 1');" },
        Path{ "a subclass of Function", "class Sub extends Function {}\nnew Sub('return 1');" },
        Path{ "AsyncFunction", "Object.getPrototypeOf(async function () {}).constructor('return 1');" },
        Path{ "AsyncFunction from an async arrow, with new", "new (Object.getPrototypeOf(async () => {}).constructor)('return 1');" },
        Path{ "GeneratorFunction", "Object.getPrototypeOf(function* () {}).constructor('yield 1');" },
        Path{ "GeneratorFunction from a generator method", "({ *m() {} }).m.constructor('yield 1');" },
        Path{ "AsyncGeneratorFunction", "Object.getPrototypeOf(async function* () {}).constructor('yield 1');" },
        Path{ "AsyncGeneratorFunction from a method, with new", "new ({ async *m() {} }).m.constructor('yield 1');" },
    };
}

TEST_CASE("every path from script code to the parser throws EvalError", "[js][codegen]")
{
    const auto& path = GENERATE(Catch::Generators::from_range(kPaths));
    INFO("path: " << path.name << "\nsource: " << path.source);
    auto runtime = Runtime::Create();

    const auto error = Thrown<Exception>([&] { runtime->EvaluateModule("codegen.js", path.source); });
    CHECK(error.Name() == "EvalError");
    CHECK_THAT(error.Message(), ContainsSubstring("SkyrimPlatform2 does not run code created from strings"));
}

TEST_CASE("script code cannot undo the lockdown", "[js][codegen]")
{
    auto runtime = Runtime::Create();

    const Value result = RunModule(*runtime, R"(
        const attempt = (body) => { try { body(); return "allowed"; } catch (e) { return e.name; } };
        const AsyncFunction = Object.getPrototypeOf(async function () {}).constructor;
        globalThis.result = {
            assignEval: attempt(() => { globalThis.eval = (source) => source; }),
            deleteEval: attempt(() => { delete globalThis.eval; }),
            redefineFunction: attempt(() => Object.defineProperty(globalThis, "Function", { value: () => {} })),
            assignConstructor: attempt(() => { Function.prototype.constructor = () => {}; }),
            redefineAsyncConstructor: attempt(() => Object.defineProperty(AsyncFunction.prototype, "constructor", { value: () => {} })),
            stillRefused: attempt(() => eval("1")),
            instanceOf: (() => {}) instanceof Function && (async () => {}) instanceof AsyncFunction,
            constructorsAgree: (function () {}).constructor === Function && Function.prototype.constructor === Function,
            names: [eval.name, Function.name, AsyncFunction.name].join(),
        };
    )");

    CHECK(result.Get("assignEval").As<std::string>() == "TypeError");
    CHECK(result.Get("deleteEval").As<std::string>() == "TypeError");
    CHECK(result.Get("redefineFunction").As<std::string>() == "TypeError");
    CHECK(result.Get("assignConstructor").As<std::string>() == "TypeError");
    CHECK(result.Get("redefineAsyncConstructor").As<std::string>() == "TypeError");
    CHECK(result.Get("stillRefused").As<std::string>() == "EvalError");
    CHECK(result.Get("instanceOf").As<bool>());
    CHECK(result.Get("constructorsAgree").As<bool>());
    CHECK(result.Get("names").As<std::string>() == "eval,Function,AsyncFunction");
}

TEST_CASE("the global object holds the engine's built-ins and nothing from the host", "[js]")
{
    // ECMAScript built-ins, plus QuickJS-ng's InternalError and queueMicrotask. An engine bump that adds a global fails here until the list is updated on purpose.
    constexpr std::string_view kGlobals =
        "AggregateError Array ArrayBuffer AsyncDisposableStack Atomics BigInt BigInt64Array BigUint64Array Boolean DataView "
        "Date DisposableStack Error EvalError FinalizationRegistry Float16Array Float32Array Float64Array Function Infinity "
        "Int16Array Int32Array Int8Array InternalError Iterator JSON Map Math NaN Number Object Promise Proxy RangeError "
        "ReferenceError Reflect RegExp Set SharedArrayBuffer String SuppressedError Symbol SyntaxError TypeError URIError "
        "Uint16Array Uint32Array Uint8Array Uint8ClampedArray WeakMap WeakRef WeakSet decodeURI decodeURIComponent "
        "encodeURI encodeURIComponent escape eval globalThis isFinite isNaN parseFloat parseInt queueMicrotask undefined "
        "unescape";
    auto runtime = Runtime::Create();

    CHECK(RunModule(*runtime, "globalThis.result = Object.getOwnPropertyNames(globalThis).sort().join(' ');").As<std::string>() == kGlobals);
}

TEST_CASE("every kind of function still works after the lockdown", "[js][codegen]")
{
    auto runtime = Runtime::Create();

    const Value result = RunModule(*runtime, R"(
        async function a() { return 1; }
        function* g() { yield 2; }
        async function* ag() { yield 3; }
        const values = [await a(), g().next().value, (await ag().next()).value, /x(y)/.exec("xy")[1], new (class { v = 6; })().v];
        globalThis.result = values.join();
    )");
    CHECK(result.As<std::string>() == "1,2,3,y,6");
}
