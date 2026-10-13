// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/json.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <string>
#include <string_view>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using sp2::core::JsonError;
using sp2::core::ParseStrictJson;
using sp2::core::Quote;

namespace
{
    std::string ErrorOf(std::string_view text)
    {
        try {
            (void)ParseStrictJson(text);
        } catch (const JsonError& error) {
            return error.what();
        }
        FAIL_CHECK("the text was accepted");
        return "(accepted)";
    }

    std::string Nested(int levels)
    {
        return std::string(static_cast<std::size_t>(levels), '[') + std::string(static_cast<std::size_t>(levels), ']');
    }
}

TEST_CASE("strict JSON reads RFC 8259 text, keeps member order and skips a byte order mark", "[json]")
{
    const auto value = ParseStrictJson("\xEF\xBB\xBF{ \"b\": 1, \"a\": [true, null, -2.5e3, \"\\u00e9\\n\"],\r\n\t\"c\": {} }");
    CHECK(value.dump() == "{\"b\":1,\"a\":[true,null,-2500.0,\"\xC3\xA9\\n\"],\"c\":{}}");
    CHECK(ParseStrictJson("{\"x\": {\"x\": 1}, \"y\": {\"x\": 2}}").dump() == R"({"x":{"x":1},"y":{"x":2}})");
    CHECK(ParseStrictJson(Nested(64)).is_array());
}

TEST_CASE("strict JSON refuses what RFC 8259 does not allow, naming the line and column", "[json]")
{
    CHECK_THAT(ErrorOf("{\"a\": 1,}"), StartsWith("line 1, column 9: syntax error"));
    CHECK_THAT(ErrorOf("{\n  \"a\": [1, 2,]\n}"), StartsWith("line 2, column 14: syntax error"));
    CHECK_THAT(ErrorOf("[1, 2] // comment"), StartsWith("line 1, column 8: syntax error"));
    CHECK_THAT(ErrorOf("{'a': 1}"), StartsWith("line 1, column 2: syntax error"));
    CHECK_THAT(ErrorOf("NaN"), StartsWith("line 1, column 1: syntax error"));
    CHECK_THAT(ErrorOf(""), StartsWith("line 1, column ") && ContainsSubstring("empty input"));
    CHECK_THAT(ErrorOf("1e999"), StartsWith("number overflow"));
}

TEST_CASE("strict JSON refuses a member name twice in one object", "[json]")
{
    CHECK(ErrorOf(R"({"a": 1, "b": {"x": 1, "x": 2}})") == R"(the name "x" appears twice in one object)");
    CHECK(ErrorOf(R"({"a": 1, "a": 1})") == R"(the name "a" appears twice in one object)");
}

TEST_CASE("strict JSON refuses nesting deeper than 64 levels", "[json]")
{
    CHECK(ErrorOf(Nested(65)) == "objects and arrays nest deeper than 64 levels");
    CHECK(ErrorOf(std::string(1000000, '[')) == "objects and arrays nest deeper than 64 levels");
}

TEST_CASE("strict JSON errors never repeat the input", "[json]")
{
    const std::string text = GENERATE(
        std::string(R"({"token": "s3cr3t-value)"),
        std::string("{\"token\": \"s3cr3t-value\x01\"}"),
        std::string("{\"token\": \"s3cr3t-value\xFF\"}"),
        std::string(R"({"token": s3cr3t-value})"),
        std::string(R"({"token": "s3cr3t-value\q"})"),
        std::string("{\"token\": \"x; expected s3cr3t-value\x01\"}"));
    INFO("text: " << text);
    const std::string error = ErrorOf(text);
    CHECK_THAT(error, StartsWith("line 1, column "));
    CHECK_THAT(error, !ContainsSubstring("s3cr3t") && !ContainsSubstring("last read") && !ContainsSubstring("json.exception"));
}

TEST_CASE("quoted text is escaped and cut short for log lines", "[json]")
{
    CHECK(Quote("plain") == R"("plain")");
    CHECK(Quote("a\"b\\c\nd\x01") == R"("a\"b\\c\nd\u0001")");
    CHECK(Quote("\xFF") == "\"\xEF\xBF\xBD\"");
    CHECK(Quote(std::string(100, 'x')) == '"' + std::string(64, 'x') + "\"...");
    CHECK(Quote(std::string(64, 'x')) == '"' + std::string(64, 'x') + '"');
    // A two-byte character across the cut is left out whole.
    CHECK(Quote(std::string(63, 'x') + "\xC3\xA9" + "tail") == '"' + std::string(63, 'x') + "\"...");
}
