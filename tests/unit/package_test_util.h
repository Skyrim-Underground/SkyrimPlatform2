// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "host/packages.h"
#include "host_test_util.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sp2::test
{
    // A fresh folder under the test work folder, deleted with everything in it when the object goes.
    class WorkFolder
    {
    public:
        explicit WorkFolder(std::string_view name);
        ~WorkFolder();

        WorkFolder(const WorkFolder&) = delete;
        WorkFolder& operator=(const WorkFolder&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }
        // Writes text to the file at relative, a UTF-8 path, creating its folders; returns the file's full path.
        std::filesystem::path Write(std::string_view relative, std::string_view text) const;
        // The full path of relative, a UTF-8 path.
        [[nodiscard]] std::filesystem::path operator/(std::string_view relative) const;

    private:
        std::filesystem::path path_;
    };

    // A minimal valid sp2.json for the package name, with extra members spliced in before the closing brace.
    [[nodiscard]] std::string Manifest(std::string_view name, std::string_view extra = "");

    // A PackageHost on a simulated clock that starts at 0, logging "[level] message" lines, over the folders <work>/Packages and <work>/Settings.
    class PackageFixture
    {
    public:
        explicit PackageFixture(std::string_view name);

        [[nodiscard]] WorkFolder& Work() noexcept { return work_; }
        [[nodiscard]] host::PackageHost& Host() noexcept { return *host_; }

        // Writes a package folder: sp2.json, then main.js unless source is empty.
        void AddPackage(std::string_view name, std::string_view source, std::string_view manifestExtra = "");
        // Discovers and starts the packages.
        void Start();
        // Advances the clock by ms, then runs the next frame with dt = ms.
        void Frame(double ms = 0);
        // The log lines written since the last call.
        [[nodiscard]] std::vector<std::string> Lines() { return sink_->Take(); }

    private:
        WorkFolder work_;
        std::shared_ptr<LineSink> sink_;
        std::shared_ptr<spdlog::logger> log_;
        js::Clock::time_point now_{};
        std::uint64_t frame_ = 0;
        // Last, so it goes first.
        std::unique_ptr<host::PackageHost> host_;
    };
}
