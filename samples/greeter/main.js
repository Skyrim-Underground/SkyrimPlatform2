// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

// A package with settings: the defaults in its sp2.json, deep-merged with Settings/greeter.json when that file exists.
// CI runs it with the override in tests/sp2-run/settings/greeter.json.
import { log, on, settings } from "skyrimPlatform2";

const { greeting, audience, everyFrames } = settings;
log(`settings: ${JSON.stringify(settings)}`);

try {
    settings.greeting = "changed";
} catch (error) {
    log(`settings are read-only: ${error.name}`);
}

on("tick", ({ frame }) => {
    if (frame % everyFrames === 0) {
        log(`${greeting}, ${audience.name}${audience.excited ? "!" : "."} (frame ${frame})`);
    }
});
