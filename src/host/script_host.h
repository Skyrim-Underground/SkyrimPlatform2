// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "core/json.h"
#include "host/inbox.h"
#include "host/js/js.h"
#include "host/timers.h"

#include <spdlog/logger.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sp2::host
{
    // The synthetic host module, the only module a package may import
    inline constexpr std::string_view kHostModuleName = "skyrimPlatform2";

    // Major version of the script API; a package's sp2.json names the one it needs as sp2Api.
    inline constexpr std::uint32_t kApiMajor = 0;

    struct HostOptions
    {
        // The package's settings, an object nesting at most core::kMaxJsonDepth levels, which the host module exports frozen as settings.
        core::Json settings = core::Json::object();
        js::RuntimeOptions runtime;
    };

    // What a host step tells the tick listeners.
    struct FrameInfo
    {
        // Host steps so far, this one included.
        std::uint64_t frame = 0;
        // Milliseconds since the previous host step; 0 in the first.
        double dtMs = 0;
        bool paused = false;
        bool inGame = false;
    };

    // A runtime with the host globals (console, function-only timers, performance.now), the skyrimPlatform2 module (log, on, once, settings), the module rules and the event loop. Used only on the thread that created it, except GetInbox().Post.
    class ScriptHost
    {
    public:
        // The clock the host reads: js::Clock in the game, a simulated one in sp2-run and the tests.
        using TimeSource = std::function<js::Clock::time_point()>;

        // Every log line starts with [package]. Throws a std::exception holding no script value if the runtime or the host globals cannot be created.
        ScriptHost(std::string package, std::shared_ptr<spdlog::logger> log, TimeSource now, const HostOptions& options = {});

        ScriptHost(const ScriptHost&) = delete;
        ScriptHost& operator=(const ScriptHost&) = delete;

        // Evaluates source as the package's module, called fileName in stacks, then runs a microtask checkpoint. Called once. False, with the error logged, if the module failed; it then never runs again.
        bool Load(std::string_view fileName, std::string_view source);

        void RunFrame(const FrameInfo& frame);

        // The module failed to load, at Load or, after a top-level await, in a later RunFrame.
        [[nodiscard]] bool HasFailed() const noexcept { return state_ == State::Failed; }

        [[nodiscard]] Inbox& GetInbox() noexcept { return inbox_; }

    private:
        enum class State
        {
            Empty,
            Evaluating,
            Running,
            Failed,
        };

        struct Listener
        {
            std::uint64_t id = 0;
            js::Value function;
            bool once = false;
        };

        void InstallGlobals();
        void InstallHostModule(const core::Json& settings);
        js::Value NewLogFunction(std::string_view name, spdlog::level::level_enum level);
        js::Value SetTimer(js::CallInfo& call, std::string_view name, bool repeat);
        js::Value AddListener(js::CallInfo& call, std::string_view name, bool once);
        void Dispatch(std::vector<Listener>& listeners, std::string_view what, std::span<const js::Value> args);
        template <class Body>
        void RunMacrotask(std::string_view what, Body&& body);
        void Checkpoint();
        void Fail(const js::Exception& error);
        void Log(spdlog::level::level_enum level, std::string_view text);

        // First, so that every value below is released before the runtime goes.
        std::unique_ptr<js::Runtime> runtime_;
        std::string package_;
        std::shared_ptr<spdlog::logger> log_;
        TimeSource now_;
        js::Clock::time_point origin_;
        Inbox inbox_;
        Timers timers_;
        std::vector<Listener> tickListeners_;
        std::vector<Listener> readyListeners_;
        std::uint64_t nextListener_ = 1;
        std::string fileName_;
        js::Value evaluation_;
        State state_ = State::Empty;
    };
}
