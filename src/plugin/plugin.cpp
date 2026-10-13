// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

#include "core/log.h"
#include "core/paths.h"
#include "core/version.h"
#include "plugin/gate.h"
#include "sp2/build_info.h"

#include <SKSE/SKSE.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>

#include <Windows.h>

// Every flag is set explicitly; plugin_binary_test.cpp checks the built DLL.
SKSE_PLUGIN_VERSION = [] {
    SKSE::PluginVersionData data{};
    data.PluginVersion({ sp2::build::kVersionMajor, sp2::build::kVersionMinor, sp2::build::kVersionPatch, 0 });
    data.PluginName(sp2::build::kProductName);
    data.AuthorName(sp2::build::kVendor);
    data.UsesAddressLibrary();
    data.UsesUpdatedStructs();
    data.versionIndependenceEx = SKSE::PluginVersionData::kVersionIndependentEx_AddressLibraryV5;
    data.MinimumRequiredXSEVersion({ sp2::plugin::kMinimumSkse[0], sp2::plugin::kMinimumSkse[1], sp2::plugin::kMinimumSkse[2], sp2::plugin::kMinimumSkse[3] });
    return data;
}();

namespace
{
    std::chrono::steady_clock::time_point g_loadStart;

    long long MillisecondsSinceLoad()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_loadStart).count();
    }

    void OnSkseMessage(SKSE::MessagingInterface::Message* message)
    {
        // Exceptions must not cross the C ABI into SKSE.
        try {
            switch (message->type) {
            case SKSE::MessagingInterface::kPostLoad:
                spdlog::info("SKSE PostLoad at +{} ms", MillisecondsSinceLoad());
                break;
            case SKSE::MessagingInterface::kDataLoaded:
                spdlog::info("SKSE DataLoaded at +{} ms", MillisecondsSinceLoad());
                break;
            default:
                break;
            }
        } catch (...) {
        }
    }

    void StartLog()
    {
        std::shared_ptr<spdlog::logger> logger;
        if (const auto directory = sp2::core::OutputDirectory()) {
            logger = sp2::core::OpenLog(*directory, sp2::core::ProfileDirectory().value_or(std::filesystem::path{}));
        }
        // Without a file the default logger gets no sinks, so neither SP2 nor CommonLibSSE-NG writes anywhere else.
        spdlog::set_default_logger(logger ? logger : std::make_shared<spdlog::logger>(std::string(sp2::build::kProductName)));
    }

    bool Load(const SKSE::LoadInterface* skse)
    {
        StartLog();
        spdlog::info("{}", sp2::core::StartupLine());

        const auto exe = sp2::core::ExecutablePath().and_then(sp2::core::ReadFileVersion);
        const auto skseVersion = sp2::core::UnpackSkseVersion(skse->SKSEVersion());
        // The Address Library file name only: SKSE refused to load SP2 if the file is missing, and the game folder is never logged.
        spdlog::info("runtime: exe {}, SKSE {}, Address Library {}", exe ? sp2::core::ToString(*exe) : "unknown", sp2::core::ToString(skseVersion),
                     exe ? sp2::core::AddressLibraryPath(*exe).filename().string() : "unknown");

        const auto allowlist = sp2::plugin::CompiledAllowlist();
        const auto rules = std::format("allowlist {}{}, SKSE {} or later", sp2::plugin::JoinVersions(allowlist), sp2::plugin::kGateTestBuild ? " (gate test build)" : "", sp2::core::ToString(sp2::plugin::kMinimumSkse));
        if (const auto refusal = sp2::plugin::Refusal(exe, skseVersion, allowlist)) {
            spdlog::warn("gate: refuse ({}): {}", rules, *refusal);
            // No MB_SETFOREGROUND or MB_TOPMOST: SP2 never forces focus.
            MessageBoxA(nullptr, refusal->c_str(), std::string(sp2::build::kProductName).c_str(), MB_OK | MB_ICONWARNING);
            spdlog::info("SKSEPlugin_Load returns false");
            return false;
        }
        spdlog::info("gate: start ({})", rules);

        // CommonLibSSE-NG's own log would be SKSE/SkyrimPlatform2.log, outside SP2's folder.
        SKSE::Init(skse, { .log = false });
        const auto* messaging = SKSE::GetMessagingInterface();
        if (messaging && messaging->RegisterListener(OnSkseMessage)) {
            spdlog::info("SKSE messaging listener registered");
        } else {
            spdlog::error("SKSE messaging listener not registered");
        }
        return true;
    }
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
    g_loadStart = std::chrono::steady_clock::now();
    // Exceptions must not cross the C ABI into SKSE; any failure leaves SP2 inactive.
    try {
        return Load(a_skse);
    } catch (const std::exception& error) {
        try {
            spdlog::critical("SKSEPlugin_Load failed: {}", error.what());
        } catch (...) {
        }
    } catch (...) {
    }
    return false;
}
