// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "host/script_host.h"

#include <catch2/catch_test_macros.hpp>

#include <spdlog/details/null_mutex.h>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sp2::test
{
    // Keeps every log line as "[level] message".
    class LineSink final : public spdlog::sinks::base_sink<spdlog::details::null_mutex>
    {
    public:
        LineSink() { set_formatter_(std::make_unique<spdlog::pattern_formatter>("[%l] %v", spdlog::pattern_time_type::local, "")); }

        [[nodiscard]] std::vector<std::string> Take() { return std::exchange(lines_, {}); }

    protected:
        void sink_it_(const spdlog::details::log_msg& message) override
        {
            spdlog::memory_buf_t text;
            formatter_->format(message, text);
            lines_.emplace_back(text.data(), text.size());
        }

        void flush_() override {}

    private:
        std::vector<std::string> lines_;
    };

    // A ScriptHost for the package "test" on a simulated clock that starts at 0.
    class HostFixture
    {
    public:
        explicit HostFixture(const host::HostOptions& options = {}) :
            sink_(std::make_shared<LineSink>()),
            log_(std::make_shared<spdlog::logger>("test", sink_))
        {
            log_->set_level(spdlog::level::debug);
            host_ = std::make_unique<host::ScriptHost>("test", log_, [this] { return now_; }, options);
        }

        [[nodiscard]] host::ScriptHost& Host() { return *host_; }

        // Loads source as main.js and requires it to succeed.
        void Load(std::string_view source) { REQUIRE(host_->Load("main.js", source)); }

        // Advances the clock by ms, then runs the next frame with dt = ms.
        void Frame(double ms = 0, bool paused = false, bool inGame = false)
        {
            now_ += std::chrono::round<js::Clock::duration>(std::chrono::duration<double, std::milli>(ms));
            host_->RunFrame({ .frame = ++frame_, .dtMs = ms, .paused = paused, .inGame = inGame });
        }

        // The log lines written since the last call.
        [[nodiscard]] std::vector<std::string> Lines() { return sink_->Take(); }

    private:
        std::shared_ptr<LineSink> sink_;
        std::shared_ptr<spdlog::logger> log_;
        js::Clock::time_point now_{};
        std::uint64_t frame_ = 0;
        // Last, so it goes first.
        std::unique_ptr<host::ScriptHost> host_;
    };
}
