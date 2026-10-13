// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

// Keeps running while the packages before it in name order are disabled.
import { log, on } from "skyrimPlatform2";

log("module body ran");
on("tick", ({ frame }) => log(`tick ${frame}`));
