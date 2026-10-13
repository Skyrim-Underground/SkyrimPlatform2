// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/package_files.h"
#include "package_test_util.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

using sp2::core::CheckPackagePath;
using sp2::core::ReadFileBelow;
using sp2::test::WorkFolder;

namespace
{
    struct Row
    {
        std::string_view path;
        std::string_view problem;
    };

    std::string Utf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    // The message of a refused read; fails the test if the read succeeds.
    std::string Refusal(const std::filesystem::path& root, std::string_view path, bool missing = false)
    {
        const auto result = ReadFileBelow(root, path);
        if (result) {
            FAIL_CHECK("the file was read");
            return "(read)";
        }
        CHECK(result.error().missing == missing);
        return result.error().message;
    }
}

TEST_CASE("package paths are relative, '/'-separated names Windows reads as written", "[paths]")
{
    for (const std::string_view path : { "main.js", "dist/main.js", "a-b_c.d/e f/main.js", "\xC3\xA9t\xC3\xA9/main.js", "...js", "console/com10/lpt.js" }) {
        INFO("path: " << path);
        CHECK(CheckPackagePath(path) == std::nullopt);
    }
}

TEST_CASE("package paths that could leave the package or name another file are refused", "[paths]")
{
    const Row row = GENERATE(
        Row{ "", "is empty" },
        Row{ "..", "contains \"..\"; package paths stay inside the package folder" },
        Row{ "../other/main.js", "contains \"..\"; package paths stay inside the package folder" },
        Row{ "dist/../../main.js", "contains \"..\"; package paths stay inside the package folder" },
        Row{ "./main.js", "contains \".\"" },
        Row{ "/main.js", "is an absolute path; package paths are relative to the package folder" },
        Row{ "//server/share/main.js", "is an absolute path; package paths are relative to the package folder" },
        Row{ "\\\\?\\C:\\main.js", "is an absolute path; package paths are relative to the package folder" },
        Row{ "C:/Games/main.js", "is an absolute path; package paths are relative to the package folder" },
        Row{ "c:main.js", "is an absolute path; package paths are relative to the package folder" },
        Row{ "dist\\main.js", "contains '\\'; package paths separate folders with '/'" },
        Row{ "dist//main.js", "has an empty folder or file name" },
        Row{ "dist/", "has an empty folder or file name" },
        Row{ "main.js:stream", "contains ':', which Windows does not allow in file names" },
        Row{ "ma*n.js", "contains '*', which Windows does not allow in file names" },
        Row{ "main\x01.js", "contains the control character U+0001" },
        Row{ "main.js.", "has the name \"main.js.\", which ends in '.' or ' '" },
        Row{ "dist /main.js", "has the name \"dist \", which ends in '.' or ' '" },
        Row{ "nul.js", "has the name \"nul.js\", which Windows reserves for a device" },
        Row{ "dist/CON", "has the name \"CON\", which Windows reserves for a device" },
        Row{ "Com1.tar.gz", "has the name \"Com1.tar.gz\", which Windows reserves for a device" },
        Row{ "lpt9 .js", "has the name \"lpt9 .js\", which Windows reserves for a device" },
        Row{ "COM\xC2\xB9.js", "has the name \"COM\xC2\xB9.js\", which Windows reserves for a device" });
    INFO("path: " << row.path);
    CHECK(CheckPackagePath(row.path) == std::optional<std::string>(row.problem));
}

TEST_CASE("a package file is read when every name on its way is a real folder or file", "[paths]")
{
    const WorkFolder work("read");
    work.Write("package/main.js", "main");
    work.Write("package/dist/lib/util.js", "util");
    work.Write("package/\xC3\xA9t\xC3\xA9/main.js", "unicode");
    const auto root = work / "package";

    CHECK(ReadFileBelow(root, "main.js") == "main");
    CHECK(ReadFileBelow(root, "dist/lib/util.js") == "util");
    CHECK(ReadFileBelow(root, "\xC3\xA9t\xC3\xA9/main.js") == "unicode");
    CHECK(ReadFileBelow(root, "MAIN.JS") == "main");

    CHECK(Refusal(root, "missing.js", true) == "\"missing.js\" does not exist");
    CHECK(Refusal(root, "nope/main.js", true) == "\"nope/main.js\" does not exist");
    CHECK(Refusal(root, "main.js/x.js", true) == "\"main.js/x.js\" does not exist");
    CHECK(Refusal(root, "dist") == "\"dist\" is a folder");
}

TEST_CASE("a package file is never read through '..' or an absolute path", "[paths]")
{
    const WorkFolder work("escape");
    const auto outside = work.Write("outside.js", "outside");
    const auto root = work / "package";
    work.Write("package/main.js", "main");

    CHECK(Refusal(root, "../outside.js") == "\"../outside.js\" contains \"..\"; package paths stay inside the package folder");
    const std::string absolute = Utf8(outside);
    CHECK(Refusal(root, absolute).ends_with(" is an absolute path; package paths are relative to the package folder"));
}
