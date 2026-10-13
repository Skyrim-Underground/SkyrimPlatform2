// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// SP2's engine-neutral script layer. Only src/engine/<engine>/ includes an engine header; one engine backend is compiled in and defines everything declared here.
namespace sp2::js
{
    class Runtime;
    class CallInfo;
    struct Backend;
    struct RuntimeState;

    // QueryPerformanceCounter on Windows.
    using Clock = std::chrono::steady_clock;

    enum class Kind
    {
        Undefined,
        Null,
        Boolean,
        Number,
        String,
        Symbol,
        BigInt,
        Object,
    };

    enum class PromiseState
    {
        NotAPromise,
        Pending,
        Fulfilled,
        Rejected,
    };

    enum class ErrorType
    {
        Error,
        TypeError,
        RangeError,
    };

    // A counted reference to a script value. It must not outlive its Runtime. A default Value is undefined.
    class Value
    {
    public:
        Value() noexcept = default;
        Value(const Value& other) noexcept;
        Value(Value&& other) noexcept;
        Value& operator=(const Value& other) noexcept;
        Value& operator=(Value&& other) noexcept;
        ~Value();

        [[nodiscard]] Kind GetKind() const noexcept;
        [[nodiscard]] bool IsUndefined() const noexcept { return GetKind() == Kind::Undefined; }
        [[nodiscard]] bool IsNull() const noexcept { return GetKind() == Kind::Null; }
        [[nodiscard]] bool IsBoolean() const noexcept { return GetKind() == Kind::Boolean; }
        [[nodiscard]] bool IsNumber() const noexcept { return GetKind() == Kind::Number; }
        [[nodiscard]] bool IsString() const noexcept { return GetKind() == Kind::String; }
        [[nodiscard]] bool IsObject() const noexcept { return GetKind() == Kind::Object; }
        [[nodiscard]] bool IsFunction() const noexcept;
        [[nodiscard]] bool IsArray() const noexcept;
        [[nodiscard]] bool IsError() const noexcept;

        // Strict conversion to bool, double, std::int32_t or std::string: any other type throws ScriptError (TypeError, or RangeError for a number that is not a 32-bit integer). Never runs script code.
        template <class T>
        [[nodiscard]] T As() const;

        // ECMAScript ToString; runs script code for objects.
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] Value Get(std::string_view key) const;
        [[nodiscard]] Value Get(std::uint32_t index) const;
        void Set(std::string_view key, const Value& value) const;
        void Set(std::uint32_t index, const Value& value) const;
        // Object.freeze.
        void Freeze() const;
        Value Call(const Value& thisValue, std::span<const Value> args = {}) const;

        [[nodiscard]] PromiseState GetPromiseState() const noexcept;
        // Fulfilment value or rejection reason; undefined while pending or for a non-promise.
        [[nodiscard]] Value PromiseResult() const;

    private:
        friend struct Backend;

        Runtime* runtime_ = nullptr;
        alignas(8) std::array<std::byte, 16> storage_{};
    };

    template <>
    [[nodiscard]] bool Value::As<bool>() const;
    template <>
    [[nodiscard]] double Value::As<double>() const;
    template <>
    [[nodiscard]] std::int32_t Value::As<std::int32_t>() const;
    template <>
    [[nodiscard]] std::string Value::As<std::string>() const;

    // Thrown by native code to raise a script error of the given type.
    class ScriptError : public std::runtime_error
    {
    public:
        ScriptError(ErrorType type, const std::string& message) :
            std::runtime_error(message),
            type_(type)
        {}

        [[nodiscard]] ErrorType Type() const noexcept { return type_; }

    private:
        ErrorType type_;
    };

    // A script exception that reached C++. what() is "name: message". It holds the thrown value, so like a Value it must not outlive its Runtime; copy the texts out first.
    class Exception : public std::runtime_error
    {
    public:
        // Reads name, message and stack from thrown. Script errors raised while reading them are swallowed.
        explicit Exception(const Value& thrown);

        [[nodiscard]] const Value& Thrown() const noexcept { return thrown_; }
        [[nodiscard]] const std::string& Name() const noexcept { return name_; }
        [[nodiscard]] const std::string& Message() const noexcept { return message_; }
        [[nodiscard]] const std::string& Stack() const noexcept { return stack_; }
        // The interrupt deadline stopped the script. Script code cannot catch this error.
        [[nodiscard]] bool Interrupted() const noexcept { return interrupted_; }

    private:
        struct Details
        {
            std::string name;
            std::string message;
            std::string stack;
            bool interrupted = false;
        };

        Exception(const Value& thrown, Details details);
        static Details Describe(const Value& thrown);

        Value thrown_;
        std::string name_;
        std::string message_;
        std::string stack_;
        bool interrupted_;
    };

    // A pending promise and the functions that settle it.
    class Deferred
    {
    public:
        Deferred(Value promise, Value resolve, Value reject) noexcept :
            promise_(std::move(promise)),
            resolve_(std::move(resolve)),
            reject_(std::move(reject))
        {}

        [[nodiscard]] const Value& Promise() const noexcept { return promise_; }
        void Resolve(const Value& value) const { resolve_.Call(Value{}, { &value, 1 }); }
        void Reject(const Value& reason) const { reject_.Call(Value{}, { &reason, 1 }); }

    private:
        Value promise_;
        Value resolve_;
        Value reject_;
    };

    // Arguments of a native function call.
    class CallInfo
    {
    public:
        CallInfo(Runtime& runtime, const Value& thisValue, std::span<const Value> args) noexcept :
            runtime_(runtime),
            this_(thisValue),
            args_(args)
        {}

        [[nodiscard]] Runtime& GetRuntime() const noexcept { return runtime_; }
        [[nodiscard]] const Value& This() const noexcept { return this_; }
        [[nodiscard]] std::size_t ArgCount() const noexcept { return args_.size(); }

        // Argument index (0-based) as T, converted with Value::As<T>; T = Value returns it unconverted. A missing argument is undefined. A wrong type throws ScriptError naming the argument.
        template <class T>
        [[nodiscard]] T Arg(std::size_t index) const
        {
            const Value& value = index < args_.size() ? args_[index] : kUndefined;
            if constexpr (std::is_same_v<T, Value>) {
                return value;
            } else {
                try {
                    return value.As<T>();
                } catch (const ScriptError& error) {
                    throw ScriptError(error.Type(), std::format("argument {}: {}", index + 1, error.what()));
                }
            }
        }

        // As Arg, but undefined or a missing argument gives std::nullopt.
        template <class T>
        [[nodiscard]] std::optional<T> OptionalArg(std::size_t index) const
        {
            if (index >= args_.size() || args_[index].IsUndefined()) {
                return std::nullopt;
            }
            return Arg<T>(index);
        }

    private:
        static inline const Value kUndefined{};

        Runtime& runtime_;
        const Value& this_;
        std::span<const Value> args_;
    };

    // A C++ function callable from script. Exceptions it throws become script errors (see Runtime). 
	// It is destroyed when the engine frees its function, or with the Runtime. 
	// It must not own Values (keep them in host state, as the host does), and destroying it must not run script code.
    using NativeFunction = std::move_only_function<Value(CallInfo&)>;

    // Maps import specifiers to module names and module names to source text. Throwing refuses the import; the error reaches the importing script. It serves static imports only: dynamic import() is always refused.
    class ModuleResolver
    {
    public:
        virtual ~ModuleResolver() = default;

        // Canonical name of the module that specifier names when module referrer imports it.
        virtual std::string Resolve(std::string_view specifier, std::string_view referrer) = 0;
        // Source text of the module name; called only for names not loaded yet.
        virtual std::string Load(std::string_view name) = 0;
    };

    struct RuntimeOptions
    {
        // Heap cap of the runtime (decision 0005, section 3).
        std::size_t memoryLimit = 256 * 1024 * 1024;
        // Native stack scripts may use, counted from where the host calls into the runtime.
        std::size_t maxStackSize = 256 * 1024;
    };

    struct Stats
    {
        std::size_t heapBytes = 0;
        std::size_t heapLimit = 0;
        std::size_t objectCount = 0;
    };

    // One engine instance with one global scope, used only on the thread that created it. 
	// Scripts cannot create code from strings: eval, Function and the async and generator function constructors throw EvalError (P3, option a).
    // Dynamic import() rejects with a TypeError whatever the specifier.
    //
    // A script exception reaching C++ is thrown as Exception. 
	// A C++ exception leaving a native function or the module resolver becomes a script error: Exception rethrows its original value, ScriptError becomes an error of its type, std::bad_alloc the engine's out-of-memory error, anything else an InternalError.
    class Runtime
    {
    public:
        [[nodiscard]] static std::unique_ptr<Runtime> Create(const RuntimeOptions& options = {});
        ~Runtime();

        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;

        [[nodiscard]] Value Global();
        [[nodiscard]] Value Null();
        [[nodiscard]] Value Boolean(bool value);
        [[nodiscard]] Value Number(double value);
        [[nodiscard]] Value String(std::string_view utf8);
        [[nodiscard]] Value NewObject();
        [[nodiscard]] Value NewArray();
        [[nodiscard]] Value NewFunction(std::string_view name, NativeFunction function);
        [[nodiscard]] Deferred NewPromise();

        [[nodiscard]] Value ParseJson(std::string_view text);
        // JSON.stringify; std::nullopt where it returns undefined.
        [[nodiscard]] std::optional<std::string> ToJson(const Value& value);

        // Without a resolver every import is refused.
        void SetModuleResolver(std::unique_ptr<ModuleResolver> resolver);
        // Registers a module called name made of host values: it exports the own enumerable properties of exports, which is frozen. When the resolver names it, Load is not called. The name must not belong to another module.
        void AddSyntheticModule(std::string_view name, const Value& exports);
        // Compiles source as an ES module called name, loads its imports through the resolver and runs it. 
		// Throws exception if any of that fails or the module body throws; otherwise returns the evaluation promise, which is pending only while a top-level await waits. 
		// Its rejection is left to the caller: TakeUnhandledRejection never reports it.
        Value EvaluateModule(std::string_view name, std::string_view source);
        // Runs queued promise jobs until none is left and returns how many ran. A queueMicrotask callback that throws, or an interrupt, is thrown as Exception; the jobs not run yet stay queued.
        std::size_t RunMicrotasks();
        // Reasons of the promises rejected while nothing handled them that still have no handler, oldest first. Each is returned once; a handler attached afterwards changes nothing.
        [[nodiscard]] std::vector<Value> TakeUnhandledRejections();

        // Script code running at or after deadline is stopped with an uncatchable error, until the deadline is moved or cleared.
        void SetInterruptDeadline(Clock::time_point deadline);
        void ClearInterruptDeadline();

        [[nodiscard]] Stats GetStats();

    private:
        friend struct Backend;

        explicit Runtime(std::unique_ptr<RuntimeState> state);

        std::unique_ptr<RuntimeState> state_;
    };
}
