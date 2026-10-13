// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

// sp2-run: runs a folder of packages outside the game for a number of frames on a simulated clock. A development and CI tool, never shipped.

#include "host/packages.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
    constexpr const char* kUsage =
        "usage: sp2-run --frames <count> [--frame-ms <milliseconds>] [--settings <folder>] [--stats] <packages folder>\n"
        "Starts every package in <packages folder> (one sub-folder each, holding its sp2.json) in name order at\n"
        "simulated time 0, then runs <count> frames, frame k at (k - 1) x <milliseconds> (default 1000/60, at most\n"
        "60000). A package's settings defaults are merged with <folder>/<package>.json when that file exists. The log\n"
        "goes to stdout; --stats then prints each package's JS time per frame.\n"
        "Exit code: 0 every package ran, 1 a package was disabled or none was found, 2 usage or internal error.\n";
    constexpr double kMaxFrameMs = 60000.0;

    struct Options
    {
        std::filesystem::path packages;
        std::filesystem::path settings;
        std::uint64_t frames = 0;
        double frameMs = 1000.0 / 60.0;
        bool stats = false;
    };

    // Narrowing a wide character blindly can turn one into an ASCII digit, so anything outside ASCII is refused.
    template <class T>
    std::optional<T> ParseNumber(std::wstring_view text)
    {
        std::string ascii;
        for (const wchar_t c : text) {
            if (c > 0x7F) {
                return std::nullopt;
            }
            ascii.push_back(static_cast<char>(c));
        }
        T value{};
        const auto [end, error] = std::from_chars(ascii.data(), ascii.data() + ascii.size(), value);
        if (error != std::errc{} || end != ascii.data() + ascii.size()) {
            return std::nullopt;
        }
        return value;
    }

    std::optional<Options> ParseOptions(int argc, wchar_t* argv[])
    {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::wstring_view arg = argv[i];
            if (arg == L"--frames" && i + 1 < argc) {
                const auto frames = ParseNumber<std::uint64_t>(argv[++i]);
                if (!frames || *frames == 0) {
                    return std::nullopt;
                }
                options.frames = *frames;
            } else if (arg == L"--frame-ms" && i + 1 < argc) {
                const auto frameMs = ParseNumber<double>(argv[++i]);
                if (!frameMs || !(*frameMs > 0 && *frameMs <= kMaxFrameMs)) {
                    return std::nullopt;
                }
                options.frameMs = *frameMs;
            } else if (arg == L"--settings" && i + 1 < argc && options.settings.empty()) {
                options.settings = argv[++i];
            } else if (arg == L"--stats") {
                options.stats = true;
            } else if (!arg.starts_with(L"--") && options.packages.empty()) {
                options.packages = arg;
            } else {
                return std::nullopt;
            }
        }
        if (options.frames == 0 || options.packages.empty()) {
            return std::nullopt;
        }
        return options;
    }

    std::string Utf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    int Run(const Options& options)
    {
        // A missing settings folder would otherwise pass silently as "no overrides".
        for (const auto* folder : { &options.packages, &options.settings }) {
            std::error_code error;
            if (!folder->empty() && !std::filesystem::is_directory(*folder, error)) {
                std::println(stderr, "sp2-run: {} is not a folder", Utf8(folder->filename()));
                return 2;
            }
        }

        auto sink = std::make_shared<spdlog::sinks::stdout_sink_st>();
        sink->set_pattern("[%l] %v");
        const auto log = std::make_shared<spdlog::logger>("sp2-run", sink);
        log->set_level(spdlog::level::debug);

        // The step is rounded to the clock's resolution, so scripts see the dt the clock actually took.
        sp2::js::Clock::time_point simulated{};
        const auto frameDuration = std::chrono::round<sp2::js::Clock::duration>(std::chrono::duration<double, std::milli>(options.frameMs));
        const double frameMs = std::chrono::duration<double, std::milli>(frameDuration).count();

        sp2::host::PackageHost host(log, [&simulated] { return simulated; });
        host.Discover({ .packages = options.packages, .settings = options.settings });
        host.Start();
        for (std::uint64_t frame = 1; frame <= options.frames; ++frame) {
            host.RunFrame({ .frame = frame, .dtMs = frame == 1 ? 0.0 : frameMs });
            simulated += frameDuration;
        }

        const auto report = host.TakeReport();
        if (options.stats) {
            for (const auto& package : report) {
                const auto& frames = package.frames;
                std::println("[stats] {}: {}, {} frames; JS time per frame p50 {:.3f} ms, p95 {:.3f} ms, p99 {:.3f} ms, max {:.3f} ms", package.name,
                             package.running ? "running" : "disabled", frames.frames, frames.p50Ms, frames.p95Ms, frames.p99Ms, frames.maxMs);
            }
        }
        const bool allRan = !report.empty() && std::ranges::all_of(report, &sp2::host::PackageReport::running);
        return allRan ? 0 : 1;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    try {
        const auto options = ParseOptions(argc, argv);
        if (!options) {
            std::print(stderr, "{}", kUsage);
            return 2;
        }
        return Run(*options);
    } catch (const std::exception& error) {
        std::println(stderr, "sp2-run: {}", error.what());
        return 2;
    }
}
