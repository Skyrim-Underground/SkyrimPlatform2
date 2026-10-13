// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include <string_view>

namespace sp2::js
{
    // Script name of the lockdown in stack traces.
    inline constexpr std::string_view kNoCodegenLockdownName = "sp2:no-codegen";

    inline constexpr std::string_view kNoCodegenLockdown = R"js("use strict";
(() => {
    const { defineProperty, getPrototypeOf } = Object;
    const Refusal = EvalError;
    const refuse = (what) => {
        throw new Refusal(`${what} is disabled: SkyrimPlatform2 does not run code created from strings`);
    };
    const lock = (target, key, value) => {
        defineProperty(target, key, { value, writable: false, enumerable: false, configurable: false });
    };
    const replaceConstructor = (name, prototype) => {
        const replacement = { [name]: function (source) { refuse(name); } }[name];
        lock(replacement, "prototype", prototype);
        lock(prototype, "constructor", replacement);
        return replacement;
    };
    lock(globalThis, "eval", { eval(source) { refuse("eval"); } }.eval);
    lock(globalThis, "Function", replaceConstructor("Function", Function.prototype));
    replaceConstructor("AsyncFunction", getPrototypeOf(async function () {}));
    replaceConstructor("GeneratorFunction", getPrototypeOf(function* () {}));
    replaceConstructor("AsyncGeneratorFunction", getPrototypeOf(async function* () {}));
})();
)js";
}
