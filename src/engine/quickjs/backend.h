// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "host/js/js.h"

#include <quickjs.h>

#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace sp2::js
{
    struct RuntimeState
    {
        struct Rejection
        {
            Value promise;
            Value reason;
        };

        JSRuntime* rt = nullptr;
        JSContext* ctx = nullptr;
        JSClassID nativeFunctionClass = 0;
        std::unique_ptr<ModuleResolver> resolver;
        std::optional<Clock::time_point> deadline;
        int depth = 0;
        // True while EvaluateModule runs and no promise job does: the engine resolves static imports only then, and dynamic import() only from a promise job.
        bool staticImports = false;
        // Rejected promises nothing handles yet, in rejection order.
        std::vector<Rejection> rejections;
        // Callables of native functions whose holder object is alive, keyed by the pointer the holder stores. The holder's finaliser erases its entry.
        std::unordered_map<NativeFunction*, std::unique_ptr<NativeFunction>> callables;

        // Drops the tracked rejections, destroys every callable, then frees the engine.
        void Close() noexcept;
    };

    struct Backend
    {
        static_assert(sizeof(JSValue) == sizeof(Value::storage_) && alignof(JSValue) <= alignof(Value));

        [[nodiscard]] static RuntimeState& State(Runtime& runtime) noexcept { return *runtime.state_; }
        [[nodiscard]] static Runtime* Owner(const Value& value) noexcept { return value.runtime_; }

        // Borrowed: no reference is added.
        [[nodiscard]] static JSValue Get(const Value& value) noexcept
        {
            if (value.runtime_ == nullptr) {
                return JS_UNDEFINED;
            }
            JSValue raw;
            std::memcpy(&raw, value.storage_.data(), sizeof(raw));
            return raw;
        }

        // Borrowed, for use in runtime; a value of another runtime throws std::invalid_argument.
        [[nodiscard]] static JSValue Get(const Value& value, const Runtime& runtime)
        {
            if (value.runtime_ != nullptr && value.runtime_ != &runtime) {
                throw std::invalid_argument("sp2::js: value belongs to another runtime");
            }
            return Get(value);
        }

        // Takes over the reference held by raw.
        [[nodiscard]] static Value Adopt(Runtime& runtime, JSValue raw) noexcept
        {
            Value value;
            value.runtime_ = &runtime;
            std::memcpy(value.storage_.data(), &raw, sizeof(raw));
            return value;
        }

        [[nodiscard]] static std::unique_ptr<Runtime> NewRuntime() { return std::unique_ptr<Runtime>(new Runtime(std::make_unique<RuntimeState>())); }
    };

    // Makes the stack limit count from the host's call into the engine; nested entries from native callbacks keep it.
    class EngineEntry
    {
    public:
        explicit EngineEntry(RuntimeState& state) noexcept :
            state_(state)
        {
            if (state_.depth++ == 0) {
                JS_UpdateStackTop(state_.rt);
            }
        }

        ~EngineEntry() { --state_.depth; }

        EngineEntry(const EngineEntry&) = delete;
        EngineEntry& operator=(const EngineEntry&) = delete;

    private:
        RuntimeState& state_;
    };

    // Takes the pending script exception and throws it as Exception.
    [[noreturn]] void ThrowPending(Runtime& runtime);

    // Adopts raw, or throws the pending exception if raw is JS_EXCEPTION.
    [[nodiscard]] Value Check(Runtime& runtime, JSValue raw);

    // Called inside a catch block: turns the C++ exception being handled into a pending script exception and returns JS_EXCEPTION.
    JSValue ThrowToScript(JSContext* ctx) noexcept;
}
