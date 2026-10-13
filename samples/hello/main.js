// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Skyrim Underground contributors

// The smallest SkyrimPlatform2 package. CI runs the samples folder with `sp2-run --frames 10 --frame-ms 20` and compares the log with tests/sp2-run/samples.expected.txt.
import { log, on, once } from "skyrimPlatform2";

log("hello from the module body");
queueMicrotask(() => log("a microtask runs as soon as the module body ends"));

once("ready", () => log("ready: the module has finished loading"));

on("tick", ({ frame, dt, paused, inGame }) => {
    if (frame <= 2 || frame % 5 === 0) {
        log(`tick ${frame}: dt ${dt} ms, paused ${paused}, inGame ${inGame}`);
    }
});

setTimeout((who) => console.log(`timeout for ${who} at ${performance.now()} ms`), 50, "hello");

let beats = 0;
const heartbeat = setInterval(() => {
    beats += 1;
    console.info(`interval ${beats} at ${performance.now()} ms`);
    Promise.resolve().then(() => console.debug(`microtask after interval ${beats}`));
    if (beats === 3) {
        clearInterval(heartbeat);
        console.warn("interval cleared");
    }
}, 40);
