// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include <catch2/catch_test_macros.hpp>

#include <quickjs.h>

#include <string_view>

// Links the static QuickJS-ng library built by ports/quickjs-ng with the static CRT and checks the pinned release.
TEST_CASE("linked QuickJS-ng is the pinned 0.17.0", "[deps]")
{
    CHECK(std::string_view(JS_GetVersion()) == "0.17.0");
    CHECK(QJS_VERSION_MAJOR == 0);
    CHECK(QJS_VERSION_MINOR == 17);
    CHECK(QJS_VERSION_PATCH == 0);
}
