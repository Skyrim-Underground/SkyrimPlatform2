// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "js_test_util.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

using namespace std::string_literals;
using Catch::Matchers::ContainsSubstring;
using sp2::js::CallInfo;
using sp2::js::Deferred;
using sp2::js::ErrorType;
using sp2::js::Exception;
using sp2::js::Kind;
using sp2::js::PromiseState;
using sp2::js::Runtime;
using sp2::js::ScriptError;
using sp2::js::Value;
using sp2::test::RunModule;
using sp2::test::Thrown;

TEST_CASE("values round-trip between C++ and script unchanged", "[js]")
{
    auto runtime = Runtime::Create();
    RunModule(*runtime, "globalThis.identity = (value) => value;");
    const Value identity = runtime->Global().Get("identity");
    const auto roundTrip = [&](const Value& value) { return identity.Call(Value{}, { &value, 1 }); };

    CHECK(roundTrip(runtime->Boolean(true)).As<bool>());
    CHECK_FALSE(roundTrip(runtime->Boolean(false)).As<bool>());
    for (const double number : { 0.0, 1.0, -7.0, 0.1, 2147483648.0, -2.5e300, 9007199254740993.0, std::numeric_limits<double>::infinity() }) {
        CHECK(roundTrip(runtime->Number(number)).As<double>() == number);
    }
    CHECK(std::isnan(roundTrip(runtime->Number(std::numeric_limits<double>::quiet_NaN())).As<double>()));
    CHECK(std::signbit(roundTrip(runtime->Number(-0.0)).As<double>()));
    CHECK(roundTrip(runtime->Number(-2147483648.0)).As<std::int32_t>() == std::numeric_limits<std::int32_t>::min());

    const std::string text = "ASCII, \xC3\x9Cnic\xC3\xB6" "de, \xE4\xB8\xAD\xE6\x96\x87, \xF0\x9F\x98\x80 and an embedded \0 byte"s;
    CHECK(roundTrip(runtime->String(text)).As<std::string>() == text);
    CHECK(roundTrip(runtime->String("")).As<std::string>().empty());
    CHECK(roundTrip(runtime->Null()).IsNull());
    CHECK(roundTrip(Value{}).IsUndefined());

    const Value object = runtime->NewObject();
    object.Set("key", runtime->Number(7));
    const Value same = roundTrip(object);
    same.Set("added", runtime->String("through the copy"));
    CHECK(object.Get("key").As<double>() == 7);
    CHECK(object.Get("added").As<std::string>() == "through the copy");
}

TEST_CASE("values report their kind", "[js]")
{
    auto runtime = Runtime::Create();
    const Value list = RunModule(*runtime, "globalThis.result = [undefined, null, true, 1.5, 'text', Symbol('s'), 10n, {}, [], () => 1, new Error('e')];");

    REQUIRE(list.IsArray());
    CHECK(list.Get(0).GetKind() == Kind::Undefined);
    CHECK(list.Get(1).GetKind() == Kind::Null);
    CHECK(list.Get(2).GetKind() == Kind::Boolean);
    CHECK(list.Get(3).GetKind() == Kind::Number);
    CHECK(list.Get(4).GetKind() == Kind::String);
    CHECK(list.Get(5).GetKind() == Kind::Symbol);
    CHECK(list.Get(6).GetKind() == Kind::BigInt);
    CHECK(list.Get(7).GetKind() == Kind::Object);
    CHECK(list.Get(8).IsArray());
    CHECK(list.Get(9).IsFunction());
    CHECK(list.Get(10).IsError());
    CHECK_FALSE(list.Get(7).IsFunction());
    CHECK_FALSE(list.Get(7).IsArray());
    CHECK(list.Get(3).ToString() == "1.5");
    CHECK(list.Get(8).ToString().empty());
    CHECK(list.Get(10).ToString() == "Error: e");
    // ECMAScript ToString refuses symbols.
    CHECK(Thrown<Exception>([&] { (void)list.Get(5).ToString(); }).Name() == "TypeError");
    CHECK(Value{}.ToString() == "undefined");
}

TEST_CASE("strict conversions refuse other types", "[js]")
{
    auto runtime = Runtime::Create();

    CHECK(std::string(Thrown<ScriptError>([&] { (void)runtime->String("1").As<double>(); }).what()) == "expected a number, got string");
    CHECK(std::string(Thrown<ScriptError>([&] { (void)runtime->Number(1).As<std::string>(); }).what()) == "expected a string, got number");
    CHECK(std::string(Thrown<ScriptError>([&] { (void)Value{}.As<bool>(); }).what()) == "expected a boolean, got undefined");
    CHECK(std::string(Thrown<ScriptError>([&] { (void)runtime->NewObject().As<std::int32_t>(); }).what()) == "expected a 32-bit integer, got object");

    const auto fraction = Thrown<ScriptError>([&] { (void)runtime->Number(1.5).As<std::int32_t>(); });
    CHECK(fraction.Type() == ErrorType::RangeError);
    CHECK(std::string(fraction.what()) == "expected a 32-bit integer, got 1.5");
    CHECK(Thrown<ScriptError>([&] { (void)runtime->Number(2147483648.0).As<std::int32_t>(); }).Type() == ErrorType::RangeError);
    CHECK(Thrown<ScriptError>([&] { (void)runtime->Number(std::numeric_limits<double>::quiet_NaN()).As<std::int32_t>(); }).Type() == ErrorType::RangeError);
    CHECK(runtime->Number(2147483647.0).As<std::int32_t>() == 2147483647);
}

TEST_CASE("objects, arrays and JSON", "[js]")
{
    auto runtime = Runtime::Create();

    const Value parsed = runtime->ParseJson(R"({"name":"Whiterun","pos":[1.5,-2,3e2],"open":true,"owner":null,"tag":"ü"})");
    CHECK(parsed.Get("name").As<std::string>() == "Whiterun");
    CHECK(parsed.Get("pos").IsArray());
    CHECK(parsed.Get("pos").Get(2).As<double>() == 300.0);
    CHECK(parsed.Get("open").As<bool>());
    CHECK(parsed.Get("owner").IsNull());
    CHECK(parsed.Get("missing").IsUndefined());
    CHECK(runtime->ToJson(parsed) == "{\"name\":\"Whiterun\",\"pos\":[1.5,-2,300],\"open\":true,\"owner\":null,\"tag\":\"\xC3\xBC\"}");

    const Value array = runtime->NewArray();
    array.Set(0, runtime->Number(1));
    array.Set(2, runtime->String("two"));
    CHECK(array.Get("length").As<std::int32_t>() == 3);
    CHECK(runtime->ToJson(array) == R"([1,null,"two"])");
    CHECK_FALSE(runtime->ToJson(Value{}).has_value());

    CHECK(Thrown<Exception>([&] { (void)runtime->ParseJson("{oops"); }).Name() == "SyntaxError");
    const Value cyclic = runtime->NewObject();
    cyclic.Set("self", cyclic);
    CHECK(Thrown<Exception>([&] { (void)runtime->ToJson(cyclic); }).Name() == "TypeError");

    const Value frozen = runtime->ParseJson(R"({"a":1})");
    frozen.Freeze();
    CHECK(Thrown<Exception>([&] { frozen.Set("a", runtime->Number(2)); }).Name() == "TypeError");
    CHECK(frozen.Get("a").As<double>() == 1);

    CHECK(std::string(Thrown<ScriptError>([] { (void)Value{}.Get("x"); }).what()) == "cannot read a property of undefined");
    CHECK(Thrown<Exception>([&] { (void)runtime->Null().Get("x"); }).Name() == "TypeError");
}

TEST_CASE("native functions take typed arguments and return values", "[js]")
{
    auto runtime = Runtime::Create();
    const Value global = runtime->Global();
    global.Set("add", runtime->NewFunction("add", [](CallInfo& call) { return call.GetRuntime().Number(call.Arg<double>(0) + call.Arg<double>(1)); }));
    global.Set("describe", runtime->NewFunction("describe", [](CallInfo& call) {
        const auto count = call.OptionalArg<std::int32_t>(1);
        return call.GetRuntime().String(std::format("{}:{}:{}", call.Arg<std::string>(0), count ? *count : -1, call.ArgCount()));
    }));
    global.Set("receiver", runtime->NewFunction("receiver", [](CallInfo& call) { return call.This(); }));
    global.Set("count", runtime->NewFunction("count", [](CallInfo& call) { return call.GetRuntime().Number(static_cast<double>(call.ArgCount())); }));

    const Value result = RunModule(*runtime, R"(
        const errorOf = (body) => { try { body(); return "no error"; } catch (e) { return `${e.name}: ${e.message}`; } };
        const holder = { receiver };
        globalThis.result = {
            sum: add(2, 3.5),
            described: describe("x", 4),
            optional: describe("y"),
            explicitUndefined: describe("z", undefined),
            manyArguments: count(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12),
            receiverKept: holder.receiver() === holder,
            name: add.name,
            wrongType: errorOf(() => add("2", 3)),
            missing: errorOf(() => add(1)),
            notInteger: errorOf(() => describe("w", 1.5)),
            notConstructor: errorOf(() => new add(1, 2)),
        };
    )");

    CHECK(result.Get("sum").As<double>() == 5.5);
    CHECK(result.Get("described").As<std::string>() == "x:4:2");
    CHECK(result.Get("optional").As<std::string>() == "y:-1:1");
    CHECK(result.Get("explicitUndefined").As<std::string>() == "z:-1:2");
    CHECK(result.Get("manyArguments").As<double>() == 12);
    CHECK(result.Get("receiverKept").As<bool>());
    CHECK(result.Get("name").As<std::string>() == "add");
    CHECK(result.Get("wrongType").As<std::string>() == "TypeError: argument 1: expected a number, got string");
    CHECK(result.Get("missing").As<std::string>() == "TypeError: argument 2: expected a number, got undefined");
    CHECK(result.Get("notInteger").As<std::string>() == "RangeError: argument 2: expected a 32-bit integer, got 1.5");
    CHECK_THAT(result.Get("notConstructor").As<std::string>(), Catch::Matchers::StartsWith("TypeError"));
}

TEST_CASE("C++ exceptions become script errors at the native boundary", "[js]")
{
    auto runtime = Runtime::Create();
    const Value global = runtime->Global();
    global.Set("fail", runtime->NewFunction("fail", [](CallInfo& call) -> Value {
        const auto kind = call.Arg<std::string>(0);
        if (kind == "type") {
            throw ScriptError(ErrorType::TypeError, "bad type");
        }
        if (kind == "range") {
            throw ScriptError(ErrorType::RangeError, "out of range");
        }
        if (kind == "long") {
            throw ScriptError(ErrorType::Error, std::string(400, 'x'));
        }
        if (kind == "std") {
            throw std::runtime_error("host bug");
        }
        if (kind == "alloc") {
            throw std::bad_alloc();
        }
        throw 42;
    }));
    global.Set("callBack", runtime->NewFunction("callBack", [](CallInfo& call) { return call.Arg<Value>(0).Call(Value{}); }));

    const Value result = RunModule(*runtime, R"(
        const caught = (body) => { try { body(); } catch (e) { return e; } };
        const describe = (e) => `${e.name}: ${e.message}`;
        const original = new Error("from script");
        const type = caught(() => fail("type"));
        globalThis.result = {
            type: describe(type),
            typeIsTypeError: type instanceof TypeError,
            typeHasStack: type.stack.includes("test.js"),
            range: describe(caught(() => fail("range"))),
            longLength: caught(() => fail("long")).message.length,
            std: describe(caught(() => fail("std"))),
            alloc: describe(caught(() => fail("alloc"))),
            other: describe(caught(() => fail("other"))),
            sameObject: caught(() => callBack(() => { throw original; })) === original,
        };
    )");

    CHECK(result.Get("type").As<std::string>() == "TypeError: bad type");
    CHECK(result.Get("typeIsTypeError").As<bool>());
    CHECK(result.Get("typeHasStack").As<bool>());
    CHECK(result.Get("range").As<std::string>() == "RangeError: out of range");
    CHECK(result.Get("longLength").As<double>() == 400);
    CHECK(result.Get("std").As<std::string>() == "InternalError: host bug");
    CHECK(result.Get("alloc").As<std::string>() == "InternalError: out of memory");
    CHECK(result.Get("other").As<std::string>() == "InternalError: unknown C++ exception");
    CHECK(result.Get("sameObject").As<bool>());
}

TEST_CASE("script exceptions reach C++ with name, message and stack", "[js]")
{
    auto runtime = Runtime::Create();

    const auto error = Thrown<Exception>([&] { runtime->EvaluateModule("stack.js", "function inner() {\n    throw new TypeError('boom');\n}\nfunction outer() { inner(); }\nouter();\n"); });
    CHECK(error.Name() == "TypeError");
    CHECK(error.Message() == "boom");
    CHECK(std::string(error.what()) == "TypeError: boom");
    CHECK_THAT(error.Stack(), ContainsSubstring("inner") && ContainsSubstring("stack.js:2"));
    CHECK_THAT(error.Stack(), ContainsSubstring("outer") && ContainsSubstring("stack.js:4"));
    CHECK(error.Thrown().IsError());
    CHECK_FALSE(error.Interrupted());

    const auto plain = Thrown<Exception>([&] { runtime->EvaluateModule("plain.js", "throw 'plain text';"); });
    CHECK(plain.Name().empty());
    CHECK(plain.Message() == "plain text");
    CHECK(std::string(plain.what()) == "plain text");
    CHECK(plain.Stack().empty());

    RunModule(*runtime, "globalThis.thrower = () => { throw new RangeError('called'); };");
    CHECK(Thrown<Exception>([&] { runtime->Global().Get("thrower").Call(Value{}); }).Name() == "RangeError");

    const auto syntax = Thrown<Exception>([&] { runtime->EvaluateModule("broken.js", "let x = ;"); });
    CHECK(syntax.Name() == "SyntaxError");
    CHECK_THAT(syntax.Stack(), ContainsSubstring("broken.js:1"));
}

TEST_CASE("promises settle through the microtask drain", "[js]")
{
    auto runtime = Runtime::Create();
    const Deferred resolved = runtime->NewPromise();
    const Deferred rejected = runtime->NewPromise();
    const Value global = runtime->Global();
    global.Set("resolved", resolved.Promise());
    global.Set("rejected", rejected.Promise());

    const Value log = RunModule(*runtime, R"(
        globalThis.result = [];
        resolved.then((value) => result.push(`resolved ${value}`));
        rejected.catch((reason) => result.push(`rejected ${reason.why}`));
        queueMicrotask(() => result.push("microtask"));
    )");
    CHECK(runtime->ToJson(log) == R"(["microtask"])");
    CHECK(resolved.Promise().GetPromiseState() == PromiseState::Pending);

    resolved.Resolve(runtime->Number(42));
    rejected.Reject(runtime->ParseJson(R"({"why":"refused"})"));
    CHECK(runtime->RunMicrotasks() == 2);
    CHECK(runtime->ToJson(log) == R"(["microtask","resolved 42","rejected refused"])");
    CHECK(resolved.Promise().GetPromiseState() == PromiseState::Fulfilled);
    CHECK(resolved.Promise().PromiseResult().As<double>() == 42);
    CHECK(rejected.Promise().GetPromiseState() == PromiseState::Rejected);
    CHECK(rejected.Promise().PromiseResult().Get("why").As<std::string>() == "refused");
    CHECK(runtime->RunMicrotasks() == 0);
    CHECK(log.GetPromiseState() == PromiseState::NotAPromise);

    RunModule(*runtime, "globalThis.twice = async (x) => { await null; return x * 2; };");
    const Value input = runtime->Number(21);
    const Value pending = runtime->Global().Get("twice").Call(Value{}, { &input, 1 });
    CHECK(pending.GetPromiseState() == PromiseState::Pending);
    runtime->RunMicrotasks();
    CHECK(pending.GetPromiseState() == PromiseState::Fulfilled);
    CHECK(pending.PromiseResult().As<double>() == 42);
}

TEST_CASE("a value of one runtime is refused by another", "[js]")
{
    auto first = Runtime::Create();
    auto second = Runtime::Create();
    const Value object = first->NewObject();

    CHECK_THROWS_AS(second->Global().Set("stray", object), std::invalid_argument);
    CHECK_THROWS_AS((void)second->ToJson(object), std::invalid_argument);
}

TEST_CASE("a native function's C++ callable is destroyed with its runtime", "[js]")
{
    auto token = std::make_shared<int>(0);
    const std::weak_ptr<int> watch = token;
    {
        auto runtime = Runtime::Create();
        runtime->Global().Set("count", runtime->NewFunction("count", [token = std::move(token)](CallInfo& call) { return call.GetRuntime().Number(++*token); }));
        CHECK(RunModule(*runtime, "count(); globalThis.result = count();").As<double>() == 2);
        CHECK_FALSE(watch.expired());
    }
    CHECK(watch.expired());
}

TEST_CASE("a native function's callable is destroyed once the host drops the function", "[js]")
{
    auto runtime = Runtime::Create();
    auto token = std::make_shared<int>(0);
    const std::weak_ptr<int> watch = token;
    {
        const Value function = runtime->NewFunction("once", [token = std::move(token)](CallInfo& call) { return call.GetRuntime().Number(++*token); });
        CHECK(function.Call(Value{}).As<double>() == 1);
    }
    CHECK(watch.expired());
}
