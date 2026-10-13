// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "core/json.h"
#include "host/frame_stats.h"
#include "host/script_host.h"

#include <spdlog/logger.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sp2::host
{
    // Where packages and their settings overrides live; in the game, Data/SkyrimPlatform2/Packages and Data/SkyrimPlatform2/Settings
    struct PackageFolders
    {
        // One sub-folder per package, named after it and holding its sp2.json.
        std::filesystem::path packages;
        // Optional <package>.json files deep-merged over the packages' settings defaults; empty for none.
        std::filesystem::path settings;
    };

    struct PackageReport
    {
        // The package's folder name; JSON-quoted when it is not a valid package name.
        std::string name;
        // Empty when the manifest was refused.
        std::string version;
        bool running = false;
        FrameSummary frames;
    };

    // A package that fails is disabled with one log line saying why, its runtime is destroyed, and the others keep running. 
	// Log lines name packages and paths inside their folders, not a full path. Used only on the thread that created it.
    class PackageHost
    {
    public:
        PackageHost(std::shared_ptr<spdlog::logger> log, ScriptHost::TimeSource now);

        PackageHost(const PackageHost&) = delete;
        PackageHost& operator=(const PackageHost&) = delete;

        // Finds the package folders and reads each one's manifest, settings and module source, in name order (byte order of the folder names). File reads only, called once.
        void Discover(const PackageFolders& folders);

        // Creates the runtime of every package Discover accepted and evaluates its module, in name order. Called once, after Discover.
        void Start();

        // One host step for each running package, in name order. Each step is timed into the package's frame stats. A package is disabled when its step throws or its module fails after a top-level await.
        void RunFrame(const FrameInfo& frame);

        // Every package Discover found, in name order, with the frames timed since the previous call.
        [[nodiscard]] std::vector<PackageReport> TakeReport();

    private:
        enum class State
        {
            Found,
            Running,
            Disabled,
        };

        struct Package
        {
            std::string name;
            std::string version;
            State state = State::Found;
            std::string main;
            std::uint32_t memoryLimitMiB = 0;
            core::Json settings;
            std::string source;
            std::unique_ptr<ScriptHost> host;
            FrameStats stats;
        };

        void Add(const std::filesystem::path& folder, const std::string& name, const PackageFolders& folders);
        void Disable(Package& package, std::string_view reason);

        std::shared_ptr<spdlog::logger> log_;
        ScriptHost::TimeSource now_;
        std::vector<Package> packages_;
    };
}
