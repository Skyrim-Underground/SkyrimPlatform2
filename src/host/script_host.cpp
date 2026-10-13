// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "host/script_host.h"

#include "host/settings.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <stdexcept>
#include <utility>

namespace sp2::host
{
    namespace
    {
        using js::CallInfo;
        using js::ErrorType;
        using js::ScriptError;
        using js::Value;

        // Browsers keep timer delays in a signed 32-bit millisecond count.
        constexpr double kMaxDelayMs = 2147483647.0;
        // Timer ids are exact in a double up to 2^53.
        constexpr double kMaxTimerId = 9007199254740992.0;

        class PackageResolver final : public js::ModuleResolver
        {
        public:
            std::string Resolve(std::string_view specifier, std::string_view) override
            {
                if (specifier != kHostModuleName) {
                    throw ScriptError(ErrorType::TypeError, std::format("cannot import '{}': a package imports only '{}'", specifier, kHostModuleName));
                }
                return std::string(kHostModuleName);
            }

            // The host module is synthetic, so no name the resolver returns is ever loaded from source.
            std::string Load(std::string_view name) override
            {
                throw ScriptError(ErrorType::TypeError, std::format("cannot load module '{}'", name));
            }
        };

        std::string WithoutTrailingNewlines(std::string text)
        {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                text.pop_back();
            }
            return text;
        }

        // "Name: message", with the stack on the following lines when there is one.
        std::string Describe(const js::Exception& error)
        {
            const std::string stack = WithoutTrailingNewlines(error.Stack());
            return stack.empty() ? std::string(error.what()) : std::format("{}\n{}", error.what(), stack);
        }

        // Script errors raised while printing a value fall back to another form; an interrupt is passed on.
        template <class Body>
        std::optional<std::string> Attempt(Body&& body)
        {
            try {
                return body();
            } catch (const js::Exception& error) {
                if (error.Interrupted()) {
                    throw;
                }
                return std::nullopt;
            }
        }

        // One console argument: strings as they are, errors with their stack, functions by name, symbols by description, other objects as JSON where possible, anything else through ECMAScript ToString.
        std::string Text(js::Runtime& runtime, const Value& value)
        {
            if (value.IsString()) {
                return value.As<std::string>();
            }
            if (value.IsError()) {
                return Describe(js::Exception(value));
            }
            if (value.IsFunction()) {
                const auto name = Attempt([&] { return value.Get("name").ToString(); }).value_or("");
                return name.empty() ? "[Function (anonymous)]" : std::format("[Function: {}]", name);
            }
            if (value.GetKind() == js::Kind::Symbol) {
                const auto description = Attempt([&] { return value.Get("description").ToString(); }).value_or("");
                return std::format("Symbol({})", description == "undefined" ? "" : description);
            }
            if (value.IsObject()) {
                if (const auto json = Attempt([&] { return runtime.ToJson(value); })) {
                    return *json;
                }
            }
            return Attempt([&] { return value.ToString(); }).value_or("[unprintable value]");
        }

        std::string Format(const CallInfo& call)
        {
            std::string line;
            for (std::size_t i = 0; i < call.ArgCount(); ++i) {
                if (i > 0) {
                    line += ' ';
                }
                line += Text(call.GetRuntime(), call.Arg<Value>(i));
            }
            return line;
        }
    }

    ScriptHost::ScriptHost(std::string package, std::shared_ptr<spdlog::logger> log, TimeSource now, const HostOptions& options) :
        runtime_(js::Runtime::Create(options.runtime)),
        package_(std::move(package)),
        log_(std::move(log)),
        now_(std::move(now)),
        origin_(now_())
    {
        runtime_->SetModuleResolver(std::make_unique<PackageResolver>());
        try {
            InstallGlobals();
            InstallHostModule(options.settings);
        } catch (const js::Exception& error) {
            // Only the text leaves: an Exception holds a value of the runtime destroyed while the constructor unwinds.
            throw std::runtime_error(std::format("the host globals could not be created: {}", error.what()));
        }
    }

    // Inbox tasks are C++ and may throw anything; nothing a macrotask throws leaves the frame.
    template <class Body>
    void ScriptHost::RunMacrotask(std::string_view what, Body&& body)
    {
        try {
            body();
        } catch (const js::Exception& error) {
            Log(spdlog::level::err, std::format("uncaught exception in {}: {}", what, Describe(error)));
        } catch (const std::exception& error) {
            Log(spdlog::level::err, std::format("uncaught C++ exception in {}: {}", what, error.what()));
        } catch (...) {
            Log(spdlog::level::err, std::format("uncaught C++ exception of unknown type in {}", what));
        }
        Checkpoint();
    }

    bool ScriptHost::Load(std::string_view fileName, std::string_view source)
    {
        if (state_ != State::Empty) {
            throw std::logic_error("ScriptHost::Load: the package's module is already loaded");
        }
        fileName_ = fileName;
        try {
            evaluation_ = runtime_->EvaluateModule(fileName, source);
        } catch (const js::Exception& error) {
            Fail(error);
            return false;
        }
        state_ = State::Evaluating;
        Checkpoint();
        if (evaluation_.GetPromiseState() == js::PromiseState::Rejected) {
            Fail(js::Exception(evaluation_.PromiseResult()));
            return false;
        }
        return true;
    }

    void ScriptHost::RunFrame(const FrameInfo& frame)
    {
        if (state_ == State::Evaluating) {
            switch (evaluation_.GetPromiseState()) {
            case js::PromiseState::Pending:
                break;
            case js::PromiseState::Fulfilled: {
                state_ = State::Running;
                evaluation_ = Value{};
                std::vector<Listener> ready = std::exchange(readyListeners_, {});
                Dispatch(ready, "a ready listener", {});
                break;
            }
            default:
                Fail(js::Exception(evaluation_.PromiseResult()));
                return;
            }
        }
        if (state_ != State::Evaluating && state_ != State::Running) {
            return;
        }

        for (Inbox::Task& task : inbox_.TakeAll()) {
            RunMacrotask("an inbox task", [&] { task(*runtime_); });
        }

        const js::Clock::time_point now = now_();
        for (const Timers::Id id : timers_.Due(now)) {
            if (auto fire = timers_.Take(id, now)) {
                RunMacrotask("a timer", [&] { (void)fire->callback.Call(Value{}, fire->args); });
            }
        }

        if (!tickListeners_.empty()) {
            const Value info = runtime_->NewObject();
            info.Set("dt", runtime_->Number(frame.dtMs));
            info.Set("frame", runtime_->Number(static_cast<double>(frame.frame)));
            info.Set("paused", runtime_->Boolean(frame.paused));
            info.Set("inGame", runtime_->Boolean(frame.inGame));
            info.Freeze();
            Dispatch(tickListeners_, "a tick listener", { &info, 1 });
        }
    }

    void ScriptHost::InstallGlobals()
    {
        js::Runtime& runtime = *runtime_;
        const Value global = runtime.Global();

        const Value console = runtime.NewObject();
        console.Set("log", NewLogFunction("log", spdlog::level::info));
        console.Set("info", NewLogFunction("info", spdlog::level::info));
        console.Set("warn", NewLogFunction("warn", spdlog::level::warn));
        console.Set("error", NewLogFunction("error", spdlog::level::err));
        console.Set("debug", NewLogFunction("debug", spdlog::level::debug));
        global.Set("console", console);

        global.Set("setTimeout", runtime.NewFunction("setTimeout", [this](CallInfo& call) { return SetTimer(call, "setTimeout", false); }));
        global.Set("setInterval", runtime.NewFunction("setInterval", [this](CallInfo& call) { return SetTimer(call, "setInterval", true); }));
        // Shared by both, as in browsers; anything but a timer id is ignored.
        const auto clear = [this](CallInfo& call) {
            const Value id = call.Arg<Value>(0);
            if (id.IsNumber()) {
                const double number = id.As<double>();
                if (number >= 1 && number <= kMaxTimerId && std::trunc(number) == number) {
                    timers_.Cancel(static_cast<Timers::Id>(number));
                }
            }
            return Value{};
        };
        global.Set("clearTimeout", runtime.NewFunction("clearTimeout", clear));
        global.Set("clearInterval", runtime.NewFunction("clearInterval", clear));

        const Value performance = runtime.NewObject();
        performance.Set("now", runtime.NewFunction("now", [this](CallInfo& call) {
            return call.GetRuntime().Number(std::chrono::duration<double, std::milli>(now_() - origin_).count());
        }));
        global.Set("performance", performance);
    }

    void ScriptHost::InstallHostModule(const core::Json& settings)
    {
        const Value exports = runtime_->NewObject();
        exports.Set("log", NewLogFunction("log", spdlog::level::info));
        exports.Set("on", runtime_->NewFunction("on", [this](CallInfo& call) { return AddListener(call, "on", false); }));
        exports.Set("once", runtime_->NewFunction("once", [this](CallInfo& call) { return AddListener(call, "once", true); }));
        exports.Set("settings", FrozenValue(*runtime_, settings));
        runtime_->AddSyntheticModule(kHostModuleName, exports);
    }

    Value ScriptHost::NewLogFunction(std::string_view name, spdlog::level::level_enum level)
    {
        return runtime_->NewFunction(name, [this, level](CallInfo& call) {
            if (log_->should_log(level)) {
                Log(level, Format(call));
            }
            return Value{};
        });
    }

    Value ScriptHost::SetTimer(CallInfo& call, std::string_view name, bool repeat)
    {
        Value callback = call.Arg<Value>(0);
        if (!callback.IsFunction()) {
            throw ScriptError(ErrorType::TypeError, std::format("{}: the handler must be a function; SkyrimPlatform2 does not run code created from strings", name));
        }
        // NaN and negative delays mean 0.
        const double requested = call.OptionalArg<double>(1).value_or(0.0);
        const double delayMs = requested > 0 ? std::min(requested, kMaxDelayMs) : 0.0;
        const auto delay = std::chrono::round<js::Clock::duration>(std::chrono::duration<double, std::milli>(delayMs));
        std::vector<Value> args;
        for (std::size_t i = 2; i < call.ArgCount(); ++i) {
            args.push_back(call.Arg<Value>(i));
        }
        const Timers::Id id = timers_.Add(now_() + delay, repeat ? std::optional(delay) : std::nullopt, std::move(callback), std::move(args));
        return call.GetRuntime().Number(static_cast<double>(id));
    }

    Value ScriptHost::AddListener(CallInfo& call, std::string_view name, bool once)
    {
        const std::string event = call.Arg<std::string>(0);
        Value listener = call.Arg<Value>(1);
        if (!listener.IsFunction()) {
            throw ScriptError(ErrorType::TypeError, std::format("{}: the listener must be a function", name));
        }
        std::vector<Listener>* listeners = event == "tick" ? &tickListeners_ : event == "ready" ? &readyListeners_ : nullptr;
        if (listeners == nullptr) {
            throw ScriptError(ErrorType::TypeError, std::format("{}: unknown event '{}'; the events are 'tick' and 'ready'", name, event));
        }
        // ready fires once; a listener added later would never run.
        if (listeners == &readyListeners_ && state_ == State::Running) {
            return Value{};
        }
        listeners->push_back({ .id = nextListener_++, .function = std::move(listener), .once = once });
        return Value{};
    }

    void ScriptHost::Dispatch(std::vector<Listener>& listeners, std::string_view what, std::span<const Value> args)
    {
        // Listeners added meanwhile wait for the next dispatch.
        const std::vector<Listener> snapshot = listeners;
        for (const Listener& listener : snapshot) {
            if (listener.once) {
                std::erase_if(listeners, [&](const Listener& entry) { return entry.id == listener.id; });
            }
            RunMacrotask(what, [&] { (void)listener.function.Call(Value{}, args); });
        }
    }

    void ScriptHost::Checkpoint()
    {
        // A job that throws to the host (a queueMicrotask callback, or an interrupted job) is logged and the remaining jobs still run.
        for (;;) {
            try {
                runtime_->RunMicrotasks();
                break;
            } catch (const js::Exception& error) {
                Log(spdlog::level::err, std::format("uncaught exception in a microtask: {}", Describe(error)));
            }
        }
        for (const Value& reason : runtime_->TakeUnhandledRejections()) {
            Log(spdlog::level::err, std::format("unhandled promise rejection: {}", Describe(js::Exception(reason))));
        }
    }

    void ScriptHost::Fail(const js::Exception& error)
    {
        Log(spdlog::level::err, std::format("{} failed to load: {}", fileName_, Describe(error)));
        state_ = State::Failed;
        evaluation_ = Value{};
    }

    void ScriptHost::Log(spdlog::level::level_enum level, std::string_view text)
    {
        log_->log(level, "[{}] {}", package_, text);
    }
}
