// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "engine/quickjs/backend.h"
#include "host/js/no_codegen.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sp2::js
{
    namespace
    {
        Runtime& RuntimeOf(void* opaque) noexcept
        {
            return *static_cast<Runtime*>(opaque);
        }

        int Interrupt(JSRuntime*, void* opaque)
        {
            const auto& deadline = Backend::State(RuntimeOf(opaque)).deadline;
            return deadline && Clock::now() >= *deadline ? 1 : 0;
        }

        // ECMAScript built-ins only (decision 0005, section 3 Globals). JS_NewContext would add atob, btoa and performance.
        bool AddIntrinsics(JSContext* ctx)
        {
            JS_AddIntrinsicRegExpCompiler(ctx);
            return JS_AddIntrinsicBaseObjects(ctx) == 0 && JS_AddIntrinsicDate(ctx) == 0 && JS_AddIntrinsicEval(ctx) == 0 &&
                   JS_AddIntrinsicRegExp(ctx) == 0 && JS_AddIntrinsicJSON(ctx) == 0 && JS_AddIntrinsicProxy(ctx) == 0 &&
                   JS_AddIntrinsicMapSet(ctx) == 0 && JS_AddIntrinsicTypedArrays(ctx) == 0 && JS_AddIntrinsicPromise(ctx) == 0 &&
                   JS_AddIntrinsicWeakRef(ctx) == 0;
        }

        void FinalizeNativeFunction(JSRuntime* rt, JSValueConst value)
        {
            auto& state = Backend::State(RuntimeOf(JS_GetRuntimeOpaque(rt)));
            state.callables.erase(static_cast<NativeFunction*>(JS_GetOpaque(value, state.nativeFunctionClass)));
        }

        // Sets RuntimeState::staticImports for one scope and restores it afterwards.
        class StaticImports
        {
        public:
            StaticImports(RuntimeState& state, bool allowed) noexcept :
                state_(state),
                saved_(std::exchange(state.staticImports, allowed))
            {}

            ~StaticImports() { state_.staticImports = saved_; }

            StaticImports(const StaticImports&) = delete;
            StaticImports& operator=(const StaticImports&) = delete;

        private:
            RuntimeState& state_;
            bool saved_;
        };

        char* ResolveModule(JSContext* ctx, const char* referrer, const char* specifier, void* opaque)
        {
            try {
                auto& state = Backend::State(RuntimeOf(opaque));
                if (!state.staticImports) {
                    throw ScriptError(ErrorType::TypeError, std::format("cannot import '{}': dynamic import() is not available", specifier));
                }
                if (!state.resolver) {
                    throw ScriptError(ErrorType::TypeError, std::format("cannot import '{}': imports are not available here", specifier));
                }
                const std::string name = state.resolver->Resolve(specifier, referrer);
                auto* copy = static_cast<char*>(js_malloc(ctx, name.size() + 1));
                if (copy != nullptr) {
                    std::memcpy(copy, name.c_str(), name.size() + 1);
                }
                return copy;
            } catch (...) {
                ThrowToScript(ctx);
                return nullptr;
            }
        }

        JSModuleDef* LoadModule(JSContext* ctx, const char* name, void* opaque)
        {
            try {
                auto& resolver = Backend::State(RuntimeOf(opaque)).resolver;
                if (!resolver) {
                    throw ScriptError(ErrorType::TypeError, std::format("cannot load module '{}': imports are not available here", name));
                }
                const std::string source = resolver->Load(name);
                const JSValue compiled = JS_Eval(ctx, source.c_str(), source.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
                if (JS_IsException(compiled)) {
                    return nullptr;
                }
                // The module stays registered with the context; the compiled value is only a handle to it.
                auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
                JS_FreeValue(ctx, compiled);
                return module;
            } catch (...) {
                ThrowToScript(ctx);
                return nullptr;
            }
        }

        int InitSyntheticModule(JSContext* ctx, JSModuleDef* module)
        {
            const JSValue exports = JS_GetModulePrivateValue(ctx, module);
            JSPropertyEnum* properties = nullptr;
            std::uint32_t count = 0;
            int result = JS_GetOwnPropertyNames(ctx, &properties, &count, exports, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);
            for (std::uint32_t i = 0; result == 0 && i < count; ++i) {
                const JSValue value = JS_GetProperty(ctx, exports, properties[i].atom);
                const char* exportName = JS_IsException(value) ? nullptr : JS_AtomToCString(ctx, properties[i].atom);
                if (exportName == nullptr) {
                    JS_FreeValue(ctx, value);
                    result = -1;
                } else {
                    result = JS_SetModuleExport(ctx, module, exportName, value);
                    JS_FreeCString(ctx, exportName);
                }
            }
            if (properties != nullptr) {
                JS_FreePropertyEnum(ctx, properties, count);
            }
            JS_FreeValue(ctx, exports);
            return result;
        }

        void TrackRejection(JSContext* ctx, JSValueConst promise, JSValueConst reason, bool isHandled, void* opaque)
        {
            Runtime& runtime = RuntimeOf(opaque);
            auto& rejections = Backend::State(runtime).rejections;
            if (isHandled) {
                std::erase_if(rejections, [&](const RuntimeState::Rejection& rejection) { return JS_VALUE_GET_PTR(Backend::Get(rejection.promise)) == JS_VALUE_GET_PTR(promise); });
                return;
            }
            try {
                rejections.push_back({ Backend::Adopt(runtime, JS_DupValue(ctx, promise)), Backend::Adopt(runtime, JS_DupValue(ctx, reason)) });
            } catch (const std::bad_alloc&) {
                // Without memory for the entry this rejection goes unreported.
            }
        }
    }

    void RuntimeState::Close() noexcept
    {
        rejections.clear();
        resolver.reset();
        // Callables own no engine values (see NativeFunction), so they can go before the engine; the finalisers that run while it is freed then erase nothing.
        callables.clear();
        if (ctx != nullptr) {
            JS_FreeContext(ctx);
            ctx = nullptr;
        }
        if (rt != nullptr) {
            JS_FreeRuntime(rt);
            rt = nullptr;
        }
    }

    Runtime::Runtime(std::unique_ptr<RuntimeState> state) :
        state_(std::move(state))
    {}

    Runtime::~Runtime()
    {
        state_->Close();
    }

    std::unique_ptr<Runtime> Runtime::Create(const RuntimeOptions& options)
    {
        auto runtime = Backend::NewRuntime();
        auto& state = *runtime->state_;

        state.rt = JS_NewRuntime();
        if (state.rt == nullptr) {
            throw std::bad_alloc();
        }
        JS_SetRuntimeOpaque(state.rt, runtime.get());
        JS_SetMemoryLimit(state.rt, options.memoryLimit);
        JS_SetMaxStackSize(state.rt, options.maxStackSize);
        JS_SetInterruptHandler(state.rt, &Interrupt, runtime.get());
        // Atomics.wait must never block the game thread: no interrupt reaches a blocked thread.
        JS_SetCanBlock(state.rt, false);
        JS_SetModuleLoaderFunc(state.rt, &ResolveModule, &LoadModule, runtime.get());
        JS_SetHostPromiseRejectionTracker(state.rt, &TrackRejection, runtime.get());

        const JSClassDef nativeFunctionClass{ .class_name = "NativeFunction", .finalizer = &FinalizeNativeFunction };
        JS_NewClassID(state.rt, &state.nativeFunctionClass);
        if (JS_NewClass(state.rt, state.nativeFunctionClass, &nativeFunctionClass) != 0) {
            throw std::bad_alloc();
        }

        state.ctx = JS_NewContextRaw(state.rt);
        if (state.ctx == nullptr) {
            throw std::bad_alloc();
        }
        JS_SetContextOpaque(state.ctx, runtime.get());
        if (!AddIntrinsics(state.ctx)) {
            throw std::runtime_error("sp2::js: could not create the built-in objects (memory limit too low?)");
        }

        EngineEntry entry(state);
        const JSValue locked = JS_Eval(state.ctx, kNoCodegenLockdown.data(), kNoCodegenLockdown.size(), kNoCodegenLockdownName.data(), JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
        if (JS_IsException(locked)) {
            // Only the text leaves: an Exception would hold a value of the runtime destroyed during unwinding.
            const std::string reason = Exception(Backend::Adopt(*runtime, JS_GetException(state.ctx))).what();
            throw std::runtime_error("sp2::js: the no-codegen lockdown failed: " + reason);
        }
        JS_FreeValue(state.ctx, locked);
        return runtime;
    }

    Value Runtime::Global()
    {
        return Backend::Adopt(*this, JS_GetGlobalObject(state_->ctx));
    }

    Value Runtime::Null()
    {
        return Backend::Adopt(*this, JS_NULL);
    }

    Value Runtime::Boolean(bool value)
    {
        return Backend::Adopt(*this, JS_NewBool(state_->ctx, value));
    }

    Value Runtime::Number(double value)
    {
        return Backend::Adopt(*this, JS_NewNumber(state_->ctx, value));
    }

    Value Runtime::String(std::string_view utf8)
    {
        return Check(*this, JS_NewStringLen(state_->ctx, utf8.data(), utf8.size()));
    }

    Value Runtime::NewObject()
    {
        return Check(*this, JS_NewObject(state_->ctx));
    }

    Value Runtime::NewArray()
    {
        return Check(*this, JS_NewArray(state_->ctx));
    }

    Deferred Runtime::NewPromise()
    {
        JSValue settle[2]{ JS_UNDEFINED, JS_UNDEFINED };
        Value promise = Check(*this, JS_NewPromiseCapability(state_->ctx, settle));
        return Deferred(std::move(promise), Backend::Adopt(*this, settle[0]), Backend::Adopt(*this, settle[1]));
    }

    Value Runtime::ParseJson(std::string_view text)
    {
        EngineEntry entry(*state_);
        const std::string terminated(text);
        return Check(*this, JS_ParseJSON(state_->ctx, terminated.c_str(), terminated.size(), "<json>"));
    }

    std::optional<std::string> Runtime::ToJson(const Value& value)
    {
        EngineEntry entry(*state_);
        const Value json = Check(*this, JS_JSONStringify(state_->ctx, Backend::Get(value, *this), JS_UNDEFINED, JS_UNDEFINED));
        if (json.IsUndefined()) {
            return std::nullopt;
        }
        return json.As<std::string>();
    }

    void Runtime::SetModuleResolver(std::unique_ptr<ModuleResolver> resolver)
    {
        state_->resolver = std::move(resolver);
    }

    void Runtime::AddSyntheticModule(std::string_view name, const Value& exports)
    {
        auto& state = *state_;
        const JSValue object = Backend::Get(exports, *this);
        if (!exports.IsObject()) {
            throw std::invalid_argument("sp2::js: the exports of a synthetic module must be an object");
        }
        EngineEntry entry(state);
        if (JS_FreezeObject(state.ctx, object) < 0) {
            ThrowPending(*this);
        }
        JSPropertyEnum* properties = nullptr;
        std::uint32_t count = 0;
        if (JS_GetOwnPropertyNames(state.ctx, &properties, &count, object, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
            ThrowPending(*this);
        }
        const auto freeProperties = [&](JSPropertyEnum* list) { JS_FreePropertyEnum(state.ctx, list, count); };
        const std::unique_ptr<JSPropertyEnum, decltype(freeProperties)> owned(properties, freeProperties);
        const std::string terminatedName(name);
        JSModuleDef* module = JS_NewCModule(state.ctx, terminatedName.c_str(), &InitSyntheticModule);
        if (module == nullptr) {
            ThrowPending(*this);
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            const char* exportName = JS_AtomToCString(state.ctx, properties[i].atom);
            const int added = exportName == nullptr ? -1 : JS_AddModuleExport(state.ctx, module, exportName);
            JS_FreeCString(state.ctx, exportName);
            if (added < 0) {
                ThrowPending(*this);
            }
        }
        JS_SetModulePrivateValue(state.ctx, module, JS_DupValue(state.ctx, object));
    }

    Value Runtime::EvaluateModule(std::string_view name, std::string_view source)
    {
        EngineEntry entry(*state_);
        const StaticImports imports(*state_, true);
        const std::string terminatedName(name);
        const std::string terminatedSource(source);
        const JSValue compiled = JS_Eval(state_->ctx, terminatedSource.c_str(), terminatedSource.size(), terminatedName.c_str(), JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled)) {
            ThrowPending(*this);
        }
        Value evaluation = Check(*this, JS_EvalFunction(state_->ctx, compiled));
        // Also while pending: a later rejection belongs to the caller, not to the unhandled rejections.
        JS_PromiseMarkAsHandled(state_->ctx, Backend::Get(evaluation));
        if (evaluation.GetPromiseState() == PromiseState::Rejected) {
            throw Exception(evaluation.PromiseResult());
        }
        return evaluation;
    }

    std::size_t Runtime::RunMicrotasks()
    {
        EngineEntry entry(*state_);
        const StaticImports imports(*state_, false);
        std::size_t ran = 0;
        for (;;) {
            JSContext* jobContext = nullptr;
            const int result = JS_ExecutePendingJob(state_->rt, &jobContext);
            if (result == 0) {
                return ran;
            }
            ++ran;
            if (result < 0) {
                ThrowPending(*this);
            }
        }
    }

    std::vector<Value> Runtime::TakeUnhandledRejections()
    {
        std::vector<Value> reasons;
        reasons.reserve(state_->rejections.size());
        for (auto& rejection : state_->rejections) {
            reasons.push_back(std::move(rejection.reason));
        }
        state_->rejections.clear();
        return reasons;
    }

    void Runtime::SetInterruptDeadline(Clock::time_point deadline)
    {
        state_->deadline = deadline;
    }

    void Runtime::ClearInterruptDeadline()
    {
        state_->deadline.reset();
    }

    Stats Runtime::GetStats()
    {
        JSMemoryUsage usage{};
        JS_ComputeMemoryUsage(state_->rt, &usage);
        return Stats{
            .heapBytes = static_cast<std::size_t>(usage.malloc_size),
            .heapLimit = static_cast<std::size_t>(usage.malloc_limit),
            .objectCount = static_cast<std::size_t>(usage.obj_count),
        };
    }
}
