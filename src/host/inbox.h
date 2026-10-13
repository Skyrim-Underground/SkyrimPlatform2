// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#pragma once

#include "host/js/js.h"

#include <deque>
#include <functional>
#include <mutex>
#include <utility>

namespace sp2::host
{
    // Work for a script's main thread, posted from any thread and run in order at the start of a frame
    // A task holds plain data only: it creates and touches script values only when it runs.
    class Inbox
    {
    public:
        using Task = std::move_only_function<void(js::Runtime&)>;

        // Any thread.
        void Post(Task task)
        {
            const std::scoped_lock lock(mutex_);
            tasks_.push_back(std::move(task));
        }

        // Main thread: the tasks posted so far, oldest first. Tasks posted meanwhile wait for the next call.
        [[nodiscard]] std::deque<Task> TakeAll()
        {
            const std::scoped_lock lock(mutex_);
            return std::exchange(tasks_, {});
        }

    private:
        std::mutex mutex_;
        std::deque<Task> tasks_;
    };
}
