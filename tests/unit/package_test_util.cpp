// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "package_test_util.h"

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <fstream>
#include <system_error>

namespace sp2::test
{
    namespace
    {
        std::filesystem::path FromUtf8(std::string_view text)
        {
            return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }
    }

    WorkFolder::WorkFolder(std::string_view name) :
        path_(std::filesystem::path(SP2_TEST_WORK_DIR).lexically_normal() / FromUtf8(name))
    {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    WorkFolder::~WorkFolder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    std::filesystem::path WorkFolder::Write(std::string_view relative, std::string_view text) const
    {
        const auto file = *this / relative;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream stream(file, std::ios::binary);
        stream << text;
        REQUIRE(stream.good());
        return file;
    }

    std::filesystem::path WorkFolder::operator/(std::string_view relative) const
    {
        return (path_ / FromUtf8(relative)).lexically_normal();
    }

    std::string Manifest(std::string_view name, std::string_view extra)
    {
        return std::format(R"({{ "name": "{}", "version": "1.0.0", "sp2Api": 0, "main": "main.js"{}{} }})", name, extra.empty() ? "" : ", ", extra);
    }

    PackageFixture::PackageFixture(std::string_view name) :
        work_(name),
        sink_(std::make_shared<LineSink>()),
        log_(std::make_shared<spdlog::logger>("test", sink_))
    {
        log_->set_level(spdlog::level::debug);
        host_ = std::make_unique<host::PackageHost>(log_, [this] { return now_; });
    }

    void PackageFixture::AddPackage(std::string_view name, std::string_view source, std::string_view manifestExtra)
    {
        work_.Write(std::format("Packages/{}/sp2.json", name), Manifest(name, manifestExtra));
        if (!source.empty()) {
            work_.Write(std::format("Packages/{}/main.js", name), source);
        }
    }

    void PackageFixture::Start()
    {
        host_->Discover({ .packages = work_ / "Packages", .settings = work_ / "Settings" });
        host_->Start();
    }

    void PackageFixture::Frame(double ms)
    {
        now_ += std::chrono::round<js::Clock::duration>(std::chrono::duration<double, std::milli>(ms));
        host_->RunFrame({ .frame = ++frame_, .dtMs = ms });
    }
}
