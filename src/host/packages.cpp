// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "host/packages.h"

#include "core/manifest.h"
#include "core/package_files.h"
#include "host/settings.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <format>
#include <optional>
#include <ranges>
#include <system_error>
#include <utility>

namespace sp2::host
{
    namespace
    {
        constexpr std::size_t kBytesPerMiB = 1024 * 1024;

        // The folder's name as UTF-8, or nothing if it is not valid UTF-16.
        std::optional<std::string> FolderName(const std::filesystem::path& folder)
        {
            try {
                const auto name = folder.filename().u8string();
                return std::string(name.begin(), name.end());
            } catch (const std::system_error&) {
                return std::nullopt;
            }
        }

        // Runs body and returns the text of what it throws, or nothing. The exception, which may hold a script value, is destroyed before this returns.
        template <class Body>
        std::optional<std::string> Failure(Body&& body)
        {
            try {
                body();
            } catch (const std::exception& error) {
                return std::string(error.what());
            } catch (...) {
                return std::string("an exception of unknown type");
            }
            return std::nullopt;
        }
    }

    PackageHost::PackageHost(std::shared_ptr<spdlog::logger> log, ScriptHost::TimeSource now) :
        log_(std::move(log)),
        now_(std::move(now))
    {}

    void PackageHost::Discover(const PackageFolders& folders)
    {
        std::vector<std::pair<std::string, std::filesystem::path>> found;
        std::error_code error;
        const auto type = std::filesystem::status(folders.packages, error).type();
        if (type == std::filesystem::file_type::directory) {
            for (std::filesystem::directory_iterator entry(folders.packages, error); !error && entry != std::filesystem::directory_iterator(); entry.increment(error)) {
                std::error_code entryError;
                if (entry->is_directory(entryError)) {
                    // No valid package name is empty, so an empty name stands for one that is not valid Unicode.
                    found.emplace_back(FolderName(entry->path()).value_or(""), entry->path());
                }
            }
        }
        if (type != std::filesystem::file_type::not_found && (type != std::filesystem::file_type::directory || error)) {
            log_->error("packages: the packages folder cannot be read");
        }
        std::ranges::sort(found, {}, [](const auto& entry) -> const std::string& { return entry.first; });
        for (const auto& [name, folder] : found) {
            Add(folder, name, folders);
        }
        if (packages_.empty()) {
            log_->info("packages: none found");
        }
    }

    void PackageHost::Add(const std::filesystem::path& folder, const std::string& name, const PackageFolders& folders)
    {
        Package& package = packages_.emplace_back();
        if (!core::IsPackageName(name)) {
            package.name = core::Quote(name);
            Disable(package, "its folder name is not a package name (1 to 64 characters of a-z, 0-9 and -, not a Windows device name such as con or com1)");
            return;
        }
        package.name = name;

        const auto text = core::ReadFileBelow(folder, core::kManifestFileName);
        if (!text) {
            Disable(package, text.error().missing ? "no sp2.json" : std::format("sp2.json: {}", text.error().message));
            return;
        }
        auto manifest = core::ParseManifest(*text, name, kApiMajor);
        if (!manifest) {
            Disable(package, std::format("sp2.json: {}", manifest.error() | std::views::join_with(std::string_view("; ")) | std::ranges::to<std::string>()));
            return;
        }
        package.version = manifest->version;

        core::Json settings = std::move(manifest->settings);
        if (!folders.settings.empty()) {
            const std::string file = name + ".json";
            const auto overrides = core::ReadFileBelow(folders.settings, file);
            if (!overrides && !overrides.error().missing) {
                Disable(package, std::format("settings file {}: {}", file, overrides.error().message));
                return;
            }
            if (overrides) {
                std::optional<std::string> problem;
                core::Json parsed;
                try {
                    parsed = core::ParseStrictJson(*overrides);
                } catch (const core::JsonError& error) {
                    problem = std::format("invalid JSON: {}", error.what());
                }
                if (!problem && !parsed.is_object()) {
                    problem = "expected an object at the top level";
                }
                if (problem) {
                    Disable(package, std::format("settings file {}: {}", file, *problem));
                    return;
                }
                settings = MergeSettings(std::move(settings), parsed);
            }
        }

        auto source = core::ReadFileBelow(folder, manifest->main);
        if (!source) {
            Disable(package, std::format("main: {}", source.error().message));
            return;
        }
        package.main = manifest->main;
        package.memoryLimitMiB = manifest->memoryLimitMiB;
        package.settings = std::move(settings);
        package.source = std::move(*source);
        log_->info("packages: {} {} found", package.name, package.version);
    }

    void PackageHost::Start()
    {
        for (Package& package : packages_) {
            if (package.state != State::Found) {
                continue;
            }
            bool loaded = false;
            const auto failure = Failure([&] {
                const HostOptions options{ .settings = std::move(package.settings), .runtime = { .memoryLimit = package.memoryLimitMiB * kBytesPerMiB } };
                package.host = std::make_unique<ScriptHost>(package.name, log_, now_, options);
                loaded = package.host->Load(package.main, package.source);
            });
            package.source = {};
            if (failure) {
                Disable(package, std::format("it could not start: {}", *failure));
            } else if (!loaded) {
                Disable(package, "its module failed to load");
            } else {
                package.state = State::Running;
                log_->info("packages: {} loaded", package.name);
            }
        }
    }

    void PackageHost::RunFrame(const FrameInfo& frame)
    {
        for (Package& package : packages_) {
            if (package.state != State::Running) {
                continue;
            }
            const auto start = js::Clock::now();
            const auto failure = Failure([&] { package.host->RunFrame(frame); });
            package.stats.Record(js::Clock::now() - start);
            if (failure) {
                Disable(package, std::format("its frame step failed: {}", *failure));
            } else if (package.host->HasFailed()) {
                Disable(package, "its module failed to load");
            }
        }
    }

    std::vector<PackageReport> PackageHost::TakeReport()
    {
        std::vector<PackageReport> report;
        for (Package& package : packages_) {
            report.push_back({ .name = package.name, .version = package.version, .running = package.state == State::Running, .frames = package.stats.Take() });
        }
        return report;
    }

    void PackageHost::Disable(Package& package, std::string_view reason)
    {
        package.state = State::Disabled;
        package.host.reset();
        package.settings = core::Json();
        package.source = {};
        log_->error("packages: {} disabled: {}", package.name, reason);
    }
}
