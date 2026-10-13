// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/log.h"
#include "package_test_util.h"
#include "sp2/build_info.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

using sp2::test::WorkFolder;

namespace
{
    std::vector<std::string> ReadLines(const std::filesystem::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        std::vector<std::string> lines;
        for (std::string line; std::getline(stream, line);) {
            if (line.ends_with('\r')) {
                line.pop_back();
            }
            lines.push_back(line);
        }
        return lines;
    }

    // One game start: open the log, write one line, close it.
    void Session(const std::filesystem::path& directory, const std::string& line)
    {
        auto logger = sp2::core::OpenLog(directory, {});
        REQUIRE(logger != nullptr);
        logger->info("{}", line);
    }

    std::filesystem::path LogFile(const std::filesystem::path& directory, int older = 0)
    {
        return directory / (older == 0 ? std::string(sp2::core::kLogFileName) : "SkyrimPlatform2." + std::to_string(older) + ".log");
    }
}

TEST_CASE("startup line names product, version and flavour without any path", "[log]")
{
    const auto line = sp2::core::StartupLine();

    CHECK(line == std::string(sp2::build::kProductName) + " " + std::string(sp2::build::kVersion) + (sp2::build::kDevBuild ? " (dev build)" : " (release build)"));
    CHECK(line.find_first_of(":/\\") == std::string::npos);
}

TEST_CASE("profile redaction replaces the profile folder in any spelling", "[log]")
{
    struct Row
    {
        std::string_view text;
        std::string_view expected;
    };
    const auto row = GENERATE(
        Row{ R"(open C:\Users\Alice\Documents\a.log failed)", R"(open %USERPROFILE%\Documents\a.log failed)" },
        Row{ "C:/Users/Alice/a.log", "%USERPROFILE%/a.log" },
        Row{ R"(c:\users\ALICE\a.log)", R"(%USERPROFILE%\a.log)" },
        Row{ R"(C:\Users\Alice and C:\Users\Alice\x)", R"(%USERPROFILE% and %USERPROFILE%\x)" },
        Row{ R"(D:\Games\Skyrim\SkyrimSE.exe)", R"(D:\Games\Skyrim\SkyrimSE.exe)" },
        Row{ R"(ends in C:\Users\Ali)", R"(ends in C:\Users\Ali)" },
        Row{ "", "" });

    CHECK(sp2::core::RedactProfile(row.text, R"(C:\Users\Alice)") == row.expected);
}

TEST_CASE("profile redaction with an empty profile leaves text unchanged", "[log]")
{
    CHECK(sp2::core::RedactProfile(R"(C:\Users\Alice\a.log)", "") == R"(C:\Users\Alice\a.log)");
}

TEST_CASE("log lines carry a timestamp, the level and the message", "[log]")
{
    const WorkFolder work("log-format");
    {
        auto logger = sp2::core::OpenLog(work.Path(), {});
        REQUIRE(logger != nullptr);
        logger->info("runtime: exe {}", "1.7.104.0");
        logger->warn("gate: refuse");
        logger->debug("below the log level");
    }

    const auto lines = ReadLines(LogFile(work.Path()));
    REQUIRE(lines.size() == 2);
    const std::regex prefix(R"(\[\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}\] )");
    CHECK(std::regex_search(lines[0], prefix, std::regex_constants::match_continuous));
    CHECK(lines[0].ends_with("] [info] runtime: exe 1.7.104.0"));
    CHECK(lines[1].ends_with("] [warning] gate: refuse"));
}

TEST_CASE("log file redacts the profile folder from every message", "[log]")
{
    const WorkFolder work("log-redaction");
    const std::filesystem::path profile = std::filesystem::path(u8"C:\\Users\\Jörg Example");
    {
        auto logger = sp2::core::OpenLog(work.Path(), profile);
        REQUIRE(logger != nullptr);
        logger->info("failed: {}", reinterpret_cast<const char*>(u8"C:\\Users\\Jörg Example\\Documents\\x.log"));
        logger->info("failed: {}", reinterpret_cast<const char*>(u8"c:/users/jörg example/y.log"));
    }

    const auto lines = ReadLines(LogFile(work.Path()));
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].ends_with(R"(failed: %USERPROFILE%\Documents\x.log)"));
    CHECK(lines[1].ends_with("failed: %USERPROFILE%/y.log"));
}

TEST_CASE("a message cannot forge a log entry or write control characters", "[log]")
{
    CHECK(sp2::core::ConfineToEntry("one\ntwo") == "one\n    two");
    CHECK(sp2::core::ConfineToEntry("a\rb\x1b[31mc\x7f") == "a\\x0Db\\x1B[31mc\\x7F");
    CHECK(sp2::core::ConfineToEntry("tab\tkept") == "tab\tkept");

    const WorkFolder work("log-forging");
    {
        auto logger = sp2::core::OpenLog(work.Path(), {});
        REQUIRE(logger != nullptr);
        logger->info("multi\n[2026-10-12 03:00:00.000] [error] forged");
    }

    const auto lines = ReadLines(LogFile(work.Path()));
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].ends_with("] [info] multi"));
    CHECK(lines[1] == "    [2026-10-12 03:00:00.000] [error] forged");
}

TEST_CASE("each start begins a new log and keeps three older ones", "[log]")
{
    const WorkFolder work("log-starts");

    for (int start = 1; start <= 5; ++start) {
        Session(work.Path(), "start " + std::to_string(start));
    }

    CHECK(ReadLines(LogFile(work.Path())).back().ends_with("start 5"));
    CHECK(ReadLines(LogFile(work.Path(), 1)).back().ends_with("start 4"));
    CHECK(ReadLines(LogFile(work.Path(), 2)).back().ends_with("start 3"));
    CHECK(ReadLines(LogFile(work.Path(), 3)).back().ends_with("start 2"));
    CHECK_FALSE(std::filesystem::exists(LogFile(work.Path(), 4)));
    CHECK(ReadLines(LogFile(work.Path())).size() == 1);
}

TEST_CASE("log rotates when a file would exceed its size limit", "[log]")
{
    const WorkFolder work("log-size");
    const sp2::core::LogOptions options{ .maxFileBytes = 200, .olderFiles = 2 };
    {
        auto logger = sp2::core::OpenLog(work.Path(), {}, options);
        REQUIRE(logger != nullptr);
        for (int line = 1; line <= 12; ++line) {
            logger->info("line {:02}", line);
        }
    }

    CHECK(std::filesystem::file_size(LogFile(work.Path())) <= 200);
    CHECK(std::filesystem::file_size(LogFile(work.Path(), 1)) <= 200);
    CHECK(std::filesystem::file_size(LogFile(work.Path(), 2)) <= 200);
    CHECK_FALSE(std::filesystem::exists(LogFile(work.Path(), 3)));
    CHECK(ReadLines(LogFile(work.Path())).back().ends_with("line 12"));
    const auto newest = ReadLines(LogFile(work.Path())).front();
    const auto previous = ReadLines(LogFile(work.Path(), 1)).back();
    CHECK(std::stoi(previous.substr(previous.size() - 2)) + 1 == std::stoi(newest.substr(newest.size() - 2)));
}

TEST_CASE("log opens in a folder whose name is outside the ANSI code page", "[log]")
{
    const WorkFolder work("log-unicode");
    const auto directory = work / "Документы ✓";

    Session(directory, "written");

    CHECK(ReadLines(LogFile(directory)).back().ends_with("written"));
}

TEST_CASE("log reports failure when the folder cannot be created", "[log]")
{
    const WorkFolder work("log-blocker");
    const auto blocker = work.Write("blocker", "a file where a folder is expected");

    CHECK(sp2::core::OpenLog(blocker / "SkyrimPlatform2", {}) == nullptr);
}
