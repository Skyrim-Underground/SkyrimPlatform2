// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "js_test_util.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>

using namespace std::chrono_literals;
using sp2::js::CallInfo;
using sp2::js::Clock;
using sp2::js::Exception;
using sp2::js::Runtime;
using sp2::js::Value;
using sp2::test::RunModule;
using sp2::test::Thrown;

namespace
{
    constexpr auto kDeadline = 100ms;
    // Far above kDeadline: the handler is polled every few thousand operations, not once per deadline.
    constexpr auto kInterruptBound = 5s;

    void CheckInterrupted(const Exception& error)
    {
        CHECK(error.Interrupted());
        CHECK(error.Name() == "InternalError");
        CHECK(error.Message() == "interrupted");
    }

    // Runs body 64 KiB times (frames + 1) further down the stack than the caller.
    template <class Body>
    void DeeperOnStack(int frames, Body& body)
    {
        volatile char padding[64 * 1024];
        padding[0] = static_cast<char>(frames);
        if (frames > 0) {
            DeeperOnStack(frames - 1, body);
        } else {
            body();
        }
        padding[sizeof(padding) - 1] = padding[0];
    }
}

TEST_CASE("a while(true) is interrupted at the deadline", "[js][limits]")
{
    auto runtime = Runtime::Create();

    const auto start = Clock::now();
    runtime->SetInterruptDeadline(start + kDeadline);
    const auto error = Thrown<Exception>([&] { runtime->EvaluateModule("spin.js", "while (true) {}"); });
    const auto elapsed = Clock::now() - start;
    CheckInterrupted(error);
    CHECK(elapsed >= kDeadline);
    CHECK(elapsed < kInterruptBound);

    runtime->ClearInterruptDeadline();
    CHECK(RunModule(*runtime, "globalThis.result = [1, 2, 3].map((x) => x * 2).join();").As<std::string>() == "2,4,6");
}

TEST_CASE("script code cannot catch the interrupt", "[js][limits]")
{
    auto runtime = Runtime::Create();
    runtime->Global().Set("invoke", runtime->NewFunction("invoke", [](CallInfo& call) { return call.Arg<Value>(0).Call(Value{}); }));

    runtime->SetInterruptDeadline(Clock::now() + kDeadline);
    CheckInterrupted(Thrown<Exception>([&] { runtime->EvaluateModule("catch.js", "try { while (true) {} } catch { globalThis.caught = 'script'; }"); }));
    CHECK(runtime->Global().Get("caught").IsUndefined());

    // The interrupt crosses a native function and stays uncatchable on the far side.
    runtime->SetInterruptDeadline(Clock::now() + kDeadline);
    CheckInterrupted(Thrown<Exception>([&] { runtime->EvaluateModule("native.js", "try { invoke(() => { for (;;) {} }); } catch { globalThis.caught = 'native'; }"); }));
    CHECK(runtime->Global().Get("caught").IsUndefined());
}

TEST_CASE("an interrupt in a promise job stops the microtask drain", "[js][limits]")
{
    auto runtime = Runtime::Create();
    runtime->EvaluateModule("jobs.js", "Promise.resolve().then(() => { for (;;) {} });\nPromise.resolve().then(() => { globalThis.later = true; });");

    runtime->SetInterruptDeadline(Clock::now() + kDeadline);
    CheckInterrupted(Thrown<Exception>([&] { runtime->RunMicrotasks(); }));
    CHECK(runtime->Global().Get("later").IsUndefined());

    runtime->ClearInterruptDeadline();
    CHECK(runtime->RunMicrotasks() >= 1);
    CHECK(runtime->Global().Get("later").As<bool>());
}

TEST_CASE("the memory cap is honoured", "[js][limits]")
{
    CHECK(Runtime::Create()->GetStats().heapLimit == 256 * 1024 * 1024);

    constexpr std::size_t kLimit = 16 * 1024 * 1024;
    auto runtime = Runtime::Create({ .memoryLimit = kLimit });
    CHECK(runtime->GetStats().heapLimit == kLimit);

    const auto error = Thrown<Exception>([&] { runtime->EvaluateModule("grow.js", "function grow() { const chunks = []; for (;;) chunks.push(new Array(4096).fill(1.5)); }\ngrow();"); });
    CHECK(error.Name() == "InternalError");
    CHECK(error.Message() == "out of memory");
    CHECK(runtime->GetStats().heapBytes <= kLimit);

    // The chunks were freed with grow's frame, so the runtime keeps working.
    CHECK(RunModule(*runtime, "globalThis.result = Array.from({ length: 1000 }, (_, i) => i).reduce((a, b) => a + b, 0);").As<double>() == 499500);
    CHECK(runtime->GetStats().heapBytes < kLimit / 2);
}

TEST_CASE("runtime stats count heap bytes and objects", "[js][limits]")
{
    auto runtime = Runtime::Create();
    const auto before = runtime->GetStats();
    RunModule(*runtime, "globalThis.result = Array.from({ length: 1000 }, (_, i) => ({ i }));");
    const auto after = runtime->GetStats();

    CHECK(after.objectCount >= before.objectCount + 1000);
    CHECK(after.heapBytes > before.heapBytes);
}

TEST_CASE("deep recursion stops at the stack limit", "[js][limits]")
{
    auto runtime = Runtime::Create();

    const auto error = Thrown<Exception>([&] { runtime->EvaluateModule("deep.js", "function down(n) { return down(n + 1) + 1; }\ndown(0);"); });
    CHECK(error.Name() == "RangeError");
    CHECK(error.Message() == "Maximum call stack size exceeded");
    CHECK(RunModule(*runtime, "globalThis.result = 'still running';").As<std::string>() == "still running");
}

TEST_CASE("the stack limit counts from where the host calls in", "[js][limits]")
{
    // Created here, used 384 KiB deeper: a limit fixed at creation (256 KiB below this frame) would refuse every call.
    auto runtime = Runtime::Create();
    std::string result;
    auto run = [&] { result = RunModule(*runtime, "globalThis.result = [1, 2, 3].map((x) => x * 2).join();").As<std::string>(); };
    DeeperOnStack(5, run);
    CHECK(result == "2,4,6");
}

TEST_CASE("Atomics.wait cannot block the thread", "[js][limits]")
{
    auto runtime = Runtime::Create();

    // With blocking allowed this would return "timed-out" after 10 ms instead of throwing.
    const auto error = Thrown<Exception>([&] { runtime->EvaluateModule("wait.js", "Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);"); });
    CHECK(error.Name() == "TypeError");
    CHECK(error.Message() == "cannot block in this thread");
}
