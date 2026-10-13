// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "js_test_util.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <format>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using sp2::js::CallInfo;
using sp2::js::ErrorType;
using sp2::js::Exception;
using sp2::js::PromiseState;
using sp2::js::Runtime;
using sp2::js::ScriptError;
using sp2::js::Value;
using sp2::test::RunModule;
using sp2::test::Thrown;

namespace
{
    struct Calls
    {
        std::vector<std::string> resolved;
        std::vector<std::string> loaded;
    };

    // Serves fixed sources; a specifier is the module name itself.
    class MapResolver : public sp2::js::ModuleResolver
    {
    public:
        MapResolver(std::map<std::string, std::string, std::less<>> sources, Calls& calls) :
            sources_(std::move(sources)),
            calls_(calls)
        {}

        std::string Resolve(std::string_view specifier, std::string_view referrer) override
        {
            calls_.resolved.push_back(std::format("{} <- {}", specifier, referrer));
            if (!sources_.contains(specifier)) {
                throw ScriptError(ErrorType::TypeError, std::format("unknown module '{}'", specifier));
            }
            return std::string(specifier);
        }

        std::string Load(std::string_view name) override
        {
            calls_.loaded.emplace_back(name);
            return sources_.find(name)->second;
        }

    private:
        std::map<std::string, std::string, std::less<>> sources_;
        Calls& calls_;
    };
}

TEST_CASE("package modules load with their imports through the resolver", "[js]")
{
    auto runtime = Runtime::Create();
    Calls calls;
    runtime->SetModuleResolver(std::make_unique<MapResolver>(
        std::map<std::string, std::string, std::less<>>{
            { "lib/math.js", "export const double = (x) => x * 2;" },
            { "lib/text.js", "import { double } from 'lib/math.js';\nexport const shout = (s) => s.toUpperCase().repeat(double(1));" },
        },
        calls));

    const Value evaluation = runtime->EvaluateModule("main.js", R"(
        import { double } from "lib/math.js";
        import { shout } from "lib/text.js";
        globalThis.result = {
            doubled: double(21),
            shouted: shout("hi"),
        };
        await Promise.resolve();
        globalThis.result.awaited = true;
    )");

    CHECK(evaluation.GetPromiseState() == PromiseState::Pending);
    runtime->RunMicrotasks();
    REQUIRE(evaluation.GetPromiseState() == PromiseState::Fulfilled);

    const Value result = runtime->Global().Get("result");
    CHECK(result.Get("doubled").As<double>() == 42);
    CHECK(result.Get("shouted").As<std::string>() == "HIHI");
    CHECK(result.Get("awaited").As<bool>());

    CHECK(calls.resolved == std::vector<std::string>{ "lib/math.js <- main.js", "lib/text.js <- main.js", "lib/math.js <- lib/text.js" });
    CHECK(calls.loaded == std::vector<std::string>{ "lib/math.js", "lib/text.js" });
}

TEST_CASE("imports are refused without a resolver", "[js]")
{
    auto runtime = Runtime::Create();

    const auto error = Thrown<Exception>([&] { runtime->EvaluateModule("main.js", "import { x } from 'anything';"); });
    CHECK(error.Name() == "TypeError");
    CHECK(error.Message() == "cannot import 'anything': imports are not available here");
}

TEST_CASE("a resolver refuses an import and the error reaches the importer", "[js]")
{
    auto runtime = Runtime::Create();
    Calls calls;
    runtime->SetModuleResolver(std::make_unique<MapResolver>(std::map<std::string, std::string, std::less<>>{ { "lib/fails.js", "throw new RangeError('lib failed');" } }, calls));

    const auto refused = Thrown<Exception>([&] { runtime->EvaluateModule("main.js", "import 'nope.js';"); });
    CHECK(refused.Name() == "TypeError");
    CHECK(refused.Message() == "unknown module 'nope.js'");

    const auto failed = Thrown<Exception>([&] { runtime->EvaluateModule("other.js", "import 'lib/fails.js';\nglobalThis.result = 'unreachable';"); });
    CHECK(failed.Name() == "RangeError");
    CHECK(failed.Message() == "lib failed");
    CHECK_THAT(failed.Stack(), ContainsSubstring("lib/fails.js"));
    CHECK(runtime->Global().Get("result").IsUndefined());
}

TEST_CASE("a synthetic module exports host values and is never loaded from source", "[js]")
{
    auto runtime = Runtime::Create();
    Calls calls;
    runtime->SetModuleResolver(std::make_unique<MapResolver>(std::map<std::string, std::string, std::less<>>{ { "host", "throw new Error('not this source');" } }, calls));
    const Value exports = runtime->NewObject();
    exports.Set("answer", runtime->Number(42));
    exports.Set("twice", runtime->NewFunction("twice", [](CallInfo& call) { return call.GetRuntime().Number(call.Arg<double>(0) * 2); }));
    runtime->AddSyntheticModule("host", exports);

    const Value result = RunModule(*runtime, R"(
        import { answer, twice } from "host";
        import * as host from "host";
        globalThis.result = `${answer} ${twice(21)} ${Object.keys(host).join()}`;
    )");
    CHECK(result.As<std::string>() == "42 42 answer,twice");
    CHECK(calls.resolved == std::vector<std::string>{ "host <- test.js" });
    CHECK(calls.loaded.empty());
    CHECK(Thrown<Exception>([&] { exports.Set("answer", runtime->Number(1)); }).Name() == "TypeError");
    CHECK_THROWS_AS(runtime->AddSyntheticModule("primitive", runtime->Number(1)), std::invalid_argument);
}

TEST_CASE("dynamic import() is refused even for a module the resolver would load", "[js]")
{
    auto runtime = Runtime::Create();
    Calls calls;
    runtime->SetModuleResolver(std::make_unique<MapResolver>(std::map<std::string, std::string, std::less<>>{ { "lib/math.js", "export const double = (x) => x * 2;" } }, calls));

    const Value result = RunModule(*runtime, R"(
        globalThis.result = [];
        await import("lib/math.js").then(() => result.push("loaded"), (e) => result.push(`${e.name}: ${e.message}`));
    )");
    CHECK(runtime->ToJson(result) == R"(["TypeError: cannot import 'lib/math.js': dynamic import() is not available"])");
    CHECK(calls.resolved.empty());
    CHECK(calls.loaded.empty());

    CHECK(RunModule(*runtime, "import { double } from 'lib/math.js';\nglobalThis.result = double(2);", "static.js").As<double>() == 4);
    CHECK(calls.loaded == std::vector<std::string>{ "lib/math.js" });
}

TEST_CASE("unhandled rejections are reported once, and never the module evaluation promise", "[js]")
{
    auto runtime = Runtime::Create();
    RunModule(*runtime, R"(
        Promise.reject(new Error("first"));
        const late = Promise.reject(new Error("handled by a later job"));
        queueMicrotask(() => late.catch(() => {}));
        globalThis.kept = Promise.reject(new Error("handled after it was reported"));
        Promise.reject(2);
    )");
    const std::vector<Value> reasons = runtime->TakeUnhandledRejections();
    REQUIRE(reasons.size() == 3);
    CHECK(reasons[0].Get("message").As<std::string>() == "first");
    CHECK(reasons[1].Get("message").As<std::string>() == "handled after it was reported");
    CHECK(reasons[2].As<double>() == 2);
    CHECK(runtime->TakeUnhandledRejections().empty());
    RunModule(*runtime, "kept.catch(() => {});", "handler.js");
    CHECK(runtime->TakeUnhandledRejections().empty());

    CHECK(Thrown<Exception>([&] { runtime->EvaluateModule("throws.js", "throw new Error('in the body');"); }).Message() == "in the body");
    const Value evaluation = runtime->EvaluateModule("awaits.js", "await new Promise((_, reject) => queueMicrotask(() => reject(new Error('after an await'))));");
    CHECK(evaluation.GetPromiseState() == PromiseState::Pending);
    runtime->RunMicrotasks();
    CHECK(evaluation.GetPromiseState() == PromiseState::Rejected);
    CHECK(runtime->TakeUnhandledRejections().empty());
}
