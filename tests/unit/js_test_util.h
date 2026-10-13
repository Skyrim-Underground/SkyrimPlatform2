// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "host/js/js.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

namespace sp2::test
{
    // Runs body and returns the E it throws; fails the test if it throws none. The result may hold script values, so it must not outlive the runtime.
    template <class E, class Body>
    E Thrown(Body&& body)
    {
        try {
            body();
        } catch (const E& error) {
            return error;
        }
        // FAIL_CHECK reports with the active INFO messages; the throw then ends the test case.
        FAIL_CHECK("the expected exception was not thrown");
        throw std::logic_error("the expected exception was not thrown");
    }

    // Evaluates source as a module, runs the microtasks it queued and returns what it stored in globalThis.result.
    inline js::Value RunModule(js::Runtime& runtime, std::string_view source, std::string_view name = "test.js")
    {
        try {
            const js::Value evaluation = runtime.EvaluateModule(name, source);
            runtime.RunMicrotasks();
            REQUIRE(evaluation.GetPromiseState() == js::PromiseState::Fulfilled);
            return runtime.Global().Get("result");
        } catch (const js::Exception& error) {
            // Rethrown as text: a script value must not outlive the runtime while the test unwinds.
            throw std::runtime_error(std::string(error.what()) + '\n' + error.Stack());
        }
    }
}
