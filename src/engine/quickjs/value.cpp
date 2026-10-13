// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "engine/quickjs/backend.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sp2::js
{
    namespace
    {
        using ErrorFactory = JSValue (*)(JSContext*, const char*, ...);

        Runtime& Require(const Value& value, std::string_view operation)
        {
            Runtime* runtime = Backend::Owner(value);
            if (runtime == nullptr) {
                throw ScriptError(ErrorType::TypeError, std::format("cannot {} undefined", operation));
            }
            return *runtime;
        }

        std::string_view TypeName(const Value& value) noexcept
        {
            switch (value.GetKind()) {
            case Kind::Undefined:
                return "undefined";
            case Kind::Null:
                return "null";
            case Kind::Boolean:
                return "boolean";
            case Kind::Number:
                return "number";
            case Kind::String:
                return "string";
            case Kind::Symbol:
                return "symbol";
            case Kind::BigInt:
                return "bigint";
            case Kind::Object:
                break;
            }
            return value.IsFunction() ? "function" : "object";
        }

        ScriptError Mismatch(std::string_view expected, const Value& value)
        {
            return ScriptError(ErrorType::TypeError, std::format("expected {}, got {}", expected, TypeName(value)));
        }

        // ToString of value, or empty if that throws; never leaves an exception pending.
        std::string TextOrEmpty(JSContext* ctx, JSValueConst value)
        {
            std::size_t length = 0;
            const char* text = JS_ToCStringLen(ctx, &length, value);
            if (text == nullptr) {
                JS_FreeValue(ctx, JS_GetException(ctx));
                return {};
            }
            std::string result(text, length);
            JS_FreeCString(ctx, text);
            return result;
        }

        std::string PropertyText(JSContext* ctx, JSValueConst object, const char* key)
        {
            const JSValue property = JS_GetPropertyStr(ctx, object, key);
            if (JS_IsException(property)) {
                JS_FreeValue(ctx, JS_GetException(ctx));
                return {};
            }
            std::string text = JS_IsUndefined(property) ? std::string() : TextOrEmpty(ctx, property);
            JS_FreeValue(ctx, property);
            return text;
        }

        ErrorFactory Factory(ErrorType type) noexcept
        {
            switch (type) {
            case ErrorType::TypeError:
                return &JS_NewTypeError;
            case ErrorType::RangeError:
                return &JS_NewRangeError;
            case ErrorType::Error:
                break;
            }
            return &JS_NewPlainError;
        }

        // Throws a new error with message in full; the engine's own formatting cuts messages at 255 bytes.
        JSValue ThrowError(JSContext* ctx, ErrorFactory factory, std::string_view message) noexcept
        {
            const JSValue error = factory(ctx, "%s", "");
            if (JS_IsException(error)) {
                return error;
            }
            const JSValue text = JS_NewStringLen(ctx, message.data(), message.size());
            if (JS_IsException(text) || JS_DefinePropertyValueStr(ctx, error, "message", text, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0) {
                JS_FreeValue(ctx, error);
                return JS_EXCEPTION;
            }
            return JS_Throw(ctx, error);
        }

        JSValue CallNative(JSContext* ctx, JSValueConst thisValue, int argc, JSValueConst* argv, int, JSValueConst* data)
        {
            Runtime& runtime = *static_cast<Runtime*>(JS_GetContextOpaque(ctx));
            try {
                auto& function = *static_cast<NativeFunction*>(JS_GetOpaque(data[0], Backend::State(runtime).nativeFunctionClass));
                std::vector<Value> args(static_cast<std::size_t>(argc));
                for (std::size_t i = 0; i < args.size(); ++i) {
                    args[i] = Backend::Adopt(runtime, JS_DupValue(ctx, argv[i]));
                }
                const Value self = Backend::Adopt(runtime, JS_DupValue(ctx, thisValue));
                CallInfo info(runtime, self, args);
                const Value result = function(info);
                return JS_DupValue(ctx, Backend::Get(result, runtime));
            } catch (...) {
                return ThrowToScript(ctx);
            }
        }
    }

    void ThrowPending(Runtime& runtime)
    {
        const JSValue error = JS_GetException(Backend::State(runtime).ctx);
        throw Exception(Backend::Adopt(runtime, JS_IsUninitialized(error) ? JS_UNDEFINED : error));
    }

    Value Check(Runtime& runtime, JSValue raw)
    {
        if (JS_IsException(raw)) {
            ThrowPending(runtime);
        }
        return Backend::Adopt(runtime, raw);
    }

    JSValue ThrowToScript(JSContext* ctx) noexcept
    {
        try {
            throw;
        } catch (const Exception& error) {
            // The original value keeps its identity, its stack and, for an interrupt, its uncatchability.
            Runtime* owner = Backend::Owner(error.Thrown());
            if (owner != nullptr && Backend::State(*owner).ctx == ctx) {
                return JS_Throw(ctx, JS_DupValue(ctx, Backend::Get(error.Thrown())));
            }
            return ThrowError(ctx, &JS_NewInternalError, error.what());
        } catch (const ScriptError& error) {
            return ThrowError(ctx, Factory(error.Type()), error.what());
        } catch (const std::bad_alloc&) {
            return JS_ThrowOutOfMemory(ctx);
        } catch (const std::exception& error) {
            return ThrowError(ctx, &JS_NewInternalError, error.what());
        } catch (...) {
            return ThrowError(ctx, &JS_NewInternalError, "unknown C++ exception");
        }
    }

    Value::Value(const Value& other) noexcept :
        runtime_(other.runtime_),
        storage_(other.storage_)
    {
        if (runtime_ != nullptr) {
            JS_DupValueRT(Backend::State(*runtime_).rt, Backend::Get(*this));
        }
    }

    Value::Value(Value&& other) noexcept :
        runtime_(std::exchange(other.runtime_, nullptr)),
        storage_(other.storage_)
    {}

    Value& Value::operator=(const Value& other) noexcept
    {
        Value copy(other);
        std::swap(runtime_, copy.runtime_);
        std::swap(storage_, copy.storage_);
        return *this;
    }

    Value& Value::operator=(Value&& other) noexcept
    {
        Value moved(std::move(other));
        std::swap(runtime_, moved.runtime_);
        std::swap(storage_, moved.storage_);
        return *this;
    }

    Value::~Value()
    {
        if (runtime_ != nullptr) {
            JS_FreeValueRT(Backend::State(*runtime_).rt, Backend::Get(*this));
        }
    }

    Kind Value::GetKind() const noexcept
    {
        switch (JS_VALUE_GET_NORM_TAG(Backend::Get(*this))) {
        case JS_TAG_NULL:
            return Kind::Null;
        case JS_TAG_BOOL:
            return Kind::Boolean;
        case JS_TAG_INT:
        case JS_TAG_FLOAT64:
            return Kind::Number;
        case JS_TAG_STRING:
        case JS_TAG_STRING_ROPE:
            return Kind::String;
        case JS_TAG_SYMBOL:
            return Kind::Symbol;
        case JS_TAG_BIG_INT:
        case JS_TAG_SHORT_BIG_INT:
            return Kind::BigInt;
        case JS_TAG_OBJECT:
            return Kind::Object;
        default:
            return Kind::Undefined;
        }
    }

    bool Value::IsFunction() const noexcept
    {
        return runtime_ != nullptr && JS_IsFunction(Backend::State(*runtime_).ctx, Backend::Get(*this));
    }

    bool Value::IsArray() const noexcept
    {
        return JS_IsArray(Backend::Get(*this));
    }

    bool Value::IsError() const noexcept
    {
        return JS_IsError(Backend::Get(*this));
    }

    template <>
    bool Value::As<bool>() const
    {
        if (!IsBoolean()) {
            throw Mismatch("a boolean", *this);
        }
        return JS_VALUE_GET_BOOL(Backend::Get(*this)) != 0;
    }

    template <>
    double Value::As<double>() const
    {
        if (!IsNumber()) {
            throw Mismatch("a number", *this);
        }
        const JSValue raw = Backend::Get(*this);
        return JS_VALUE_GET_TAG(raw) == JS_TAG_INT ? JS_VALUE_GET_INT(raw) : JS_VALUE_GET_FLOAT64(raw);
    }

    template <>
    std::int32_t Value::As<std::int32_t>() const
    {
        if (!IsNumber()) {
            throw Mismatch("a 32-bit integer", *this);
        }
        const double number = As<double>();
        constexpr double kMin = std::numeric_limits<std::int32_t>::min();
        constexpr double kMax = std::numeric_limits<std::int32_t>::max();
        if (!(number >= kMin && number <= kMax) || std::trunc(number) != number) {
            throw ScriptError(ErrorType::RangeError, std::format("expected a 32-bit integer, got {}", number));
        }
        return static_cast<std::int32_t>(number);
    }

    template <>
    std::string Value::As<std::string>() const
    {
        if (!IsString()) {
            throw Mismatch("a string", *this);
        }
        auto& state = Backend::State(*runtime_);
        std::size_t length = 0;
        const char* text = JS_ToCStringLen(state.ctx, &length, Backend::Get(*this));
        if (text == nullptr) {
            ThrowPending(*runtime_);
        }
        std::string result(text, length);
        JS_FreeCString(state.ctx, text);
        return result;
    }

    std::string Value::ToString() const
    {
        if (runtime_ == nullptr) {
            return "undefined";
        }
        auto& state = Backend::State(*runtime_);
        EngineEntry entry(state);
        std::size_t length = 0;
        const char* text = JS_ToCStringLen(state.ctx, &length, Backend::Get(*this));
        if (text == nullptr) {
            ThrowPending(*runtime_);
        }
        std::string result(text, length);
        JS_FreeCString(state.ctx, text);
        return result;
    }

    Value Value::Get(std::string_view key) const
    {
        Runtime& runtime = Require(*this, "read a property of");
        auto& state = Backend::State(runtime);
        EngineEntry entry(state);
        const JSAtom atom = JS_NewAtomLen(state.ctx, key.data(), key.size());
        if (atom == JS_ATOM_NULL) {
            ThrowPending(runtime);
        }
        const JSValue result = JS_GetProperty(state.ctx, Backend::Get(*this), atom);
        JS_FreeAtom(state.ctx, atom);
        return Check(runtime, result);
    }

    Value Value::Get(std::uint32_t index) const
    {
        Runtime& runtime = Require(*this, "read an element of");
        auto& state = Backend::State(runtime);
        EngineEntry entry(state);
        return Check(runtime, JS_GetPropertyUint32(state.ctx, Backend::Get(*this), index));
    }

    void Value::Set(std::string_view key, const Value& value) const
    {
        Runtime& runtime = Require(*this, "set a property of");
        auto& state = Backend::State(runtime);
        const JSValue item = Backend::Get(value, runtime);
        EngineEntry entry(state);
        const JSAtom atom = JS_NewAtomLen(state.ctx, key.data(), key.size());
        if (atom == JS_ATOM_NULL) {
            ThrowPending(runtime);
        }
        const int result = JS_SetProperty(state.ctx, Backend::Get(*this), atom, JS_DupValue(state.ctx, item));
        JS_FreeAtom(state.ctx, atom);
        if (result < 0) {
            ThrowPending(runtime);
        }
    }

    void Value::Set(std::uint32_t index, const Value& value) const
    {
        Runtime& runtime = Require(*this, "set an element of");
        auto& state = Backend::State(runtime);
        const JSValue item = Backend::Get(value, runtime);
        EngineEntry entry(state);
        if (JS_SetPropertyUint32(state.ctx, Backend::Get(*this), index, JS_DupValue(state.ctx, item)) < 0) {
            ThrowPending(runtime);
        }
    }

    void Value::Freeze() const
    {
        if (runtime_ == nullptr) {
            return;
        }
        auto& state = Backend::State(*runtime_);
        EngineEntry entry(state);
        if (JS_FreezeObject(state.ctx, Backend::Get(*this)) < 0) {
            ThrowPending(*runtime_);
        }
    }

    Value Value::Call(const Value& thisValue, std::span<const Value> args) const
    {
        Runtime& runtime = Require(*this, "call");
        auto& state = Backend::State(runtime);
        std::vector<JSValue> raw(args.size());
        for (std::size_t i = 0; i < args.size(); ++i) {
            raw[i] = Backend::Get(args[i], runtime);
        }
        const JSValue self = Backend::Get(thisValue, runtime);
        EngineEntry entry(state);
        return Check(runtime, JS_Call(state.ctx, Backend::Get(*this), self, static_cast<int>(args.size()), raw.data()));
    }

    PromiseState Value::GetPromiseState() const noexcept
    {
        if (runtime_ == nullptr) {
            return PromiseState::NotAPromise;
        }
        switch (JS_PromiseState(Backend::State(*runtime_).ctx, Backend::Get(*this))) {
        case JS_PROMISE_PENDING:
            return PromiseState::Pending;
        case JS_PROMISE_FULFILLED:
            return PromiseState::Fulfilled;
        case JS_PROMISE_REJECTED:
            return PromiseState::Rejected;
        case JS_PROMISE_NOT_A_PROMISE:
            break;
        }
        return PromiseState::NotAPromise;
    }

    Value Value::PromiseResult() const
    {
        if (runtime_ == nullptr) {
            return {};
        }
        return Backend::Adopt(*runtime_, JS_PromiseResult(Backend::State(*runtime_).ctx, Backend::Get(*this)));
    }

    Exception::Exception(const Value& thrown) :
        Exception(thrown, Describe(thrown))
    {}

    Exception::Exception(const Value& thrown, Details details) :
        std::runtime_error(details.name.empty() ? details.message : details.name + ": " + details.message),
        thrown_(thrown),
        name_(std::move(details.name)),
        message_(std::move(details.message)),
        stack_(std::move(details.stack)),
        interrupted_(details.interrupted)
    {}

    Exception::Details Exception::Describe(const Value& thrown)
    {
        Runtime* runtime = Backend::Owner(thrown);
        if (runtime == nullptr) {
            return Details{ .message = "undefined" };
        }
        auto& state = Backend::State(*runtime);
        EngineEntry entry(state);
        const JSValue raw = Backend::Get(thrown);
        if (!JS_IsError(raw)) {
            return Details{ .message = TextOrEmpty(state.ctx, raw) };
        }
        return Details{
            .name = PropertyText(state.ctx, raw, "name"),
            .message = PropertyText(state.ctx, raw, "message"),
            .stack = PropertyText(state.ctx, raw, "stack"),
            .interrupted = JS_IsUncatchableError(raw),
        };
    }

    Value Runtime::NewFunction(std::string_view name, NativeFunction function)
    {
        auto& state = *state_;
        auto callable = std::make_unique<NativeFunction>(std::move(function));
        NativeFunction* const key = callable.get();
        const std::string terminatedName(name);
        state.callables.emplace(key, std::move(callable));
        JSValue holder = JS_NewObjectClass(state.ctx, state.nativeFunctionClass);
        if (JS_IsException(holder)) {
            state.callables.erase(key);
            ThrowPending(*this);
        }
        // From here the holder's finaliser destroys the callable.
        JS_SetOpaque(holder, key);
        const JSValue created = JS_NewCFunctionData2(state.ctx, &CallNative, terminatedName.c_str(), 0, 0, 1, &holder);
        JS_FreeValue(state.ctx, holder);
        return Check(*this, created);
    }
}
