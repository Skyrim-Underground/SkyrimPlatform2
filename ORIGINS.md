<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Skyrim Underground contributors -->

# Origins

SkyrimPlatform2 has no shared history with skymp or SkyrimPlatform, so this file records where anything not written for SkyrimPlatform2 comes from. 
Update it in the same change that adds, removes or bumps third-party material.

No code has been taken from SkyrimPlatform, skymp, Alduinak or Tilted Phoques (`src/tilted`).

## Third-party text in this repository

`LICENSE` (the GNU AGPL v3 text) and `LICENSES/AGPL-3.0-or-later.txt`
Free Software Foundation. Copied on 2026-10-11 from `github/choosealicense.com` `_licenses/agpl-3.0.txt` (front matter removed); identical, after line-ending normalisation, to the AGPL copy in skymp's `skymp5-server/LICENSE` FSF: verbatim copying permitted, changes not allowed.

`LICENSE`, additional permission at the end written for SkyrimPlatform2 AGPL-3.0-or-later
Follows the FSF's template for GPL section 7 linking permissions. Its scope and structure were informed by CommonLibSSE-NG's `EXCEPTIONS.md` at v12.1.0 (Modding Exception and GPL-3.0 Linking Exception); no text was copied from it/

## Interface facts used by SkyrimPlatform2's own code

| Path | Source | What was used |
|---|---|---|
| `tests/unit/plugin_binary_test.cpp` | SKSE64 `skse64/PluginAPI.h` (ianpatt/skse64, master, read 2026-10-11) | Field order and sizes of `SKSEPluginVersionData` and the values of its flags and version packing, re-declared independently. No SKSE code is included |
| `src/core/version.h`, `tests/unit/version_test.cpp` | SKSE64 `skse64_common/skse_version.h` at tag v2.3.1 (commit `7ff865f4a27d6dc936ab5fd0533ff2d706c8f857`, read 2026-10-11) | The `MAKE_EXE_VERSION_EX` bit layout and the packed values of SKSE 2.3.1 and runtimes 1.7.99, 1.7.104 and GOG 1.6.1179, re-implemented and used as test values. No SKSE code is included |
| `src/core/paths.cpp` | SKSE64 `skse64/PluginManager.cpp` at tag v2.3.1; CommonLibSSE-NG v12.1.0 `include/REL/IDDB.h` | The Address Library file name pattern `versionlib-<a>-<b>-<c>-<d>.bin` under `Data/SKSE/Plugins` |
| `docs/testing/session-A.md` | SKSE64 `skse64/PluginManager.cpp`, `skse64/skse64.cpp`, `skse64_loader/main.cpp` at tag v2.3.1 | Short quotations of SKSE's own log and message-box wording, as expected results to compare against |
| `src/plugin/plugin.cpp` | CommonLibSSE-NG v12.1.0 `include/SKSE/Interfaces.h`, `include/SKSE/API.h` | Uses NG's public `SKSE::PluginVersionData`, `SKSE::Init` and `MessagingInterface` API |
| `src/engine/quickjs/`, `src/host/js/no_codegen.h` | QuickJS-ng v0.17.0 `quickjs.h`, `quickjs.c` and `quickjs-libc.c` from the pinned source archive (read 2026-10-11 and 2026-10-12) | QuickJS-ng's public C API. Which built-ins reach the parser (global `eval`; `js_function_constructor` behind `Function` and the async and generator function constructors; direct eval only through the original `eval` object), how interrupts and uncatchable errors propagate, and the default of `JS_SetCanBlock`. The compile-only module loading sequence (`JS_Eval` with `JS_EVAL_TYPE_MODULE \| JS_EVAL_FLAG_COMPILE_ONLY`, then the module pointer from the value, then freeing the value) follows the pattern of quickjs-libc's module loader. For M3: C modules (`JS_NewCModule`, `JS_AddModuleExport`, `JS_SetModuleExport`, module private values), that static imports resolve at compile time while `import()` resolves from a queued job, when the promise rejection tracker is called, and that a throwing `queueMicrotask` callback fails its job. No QuickJS-ng code is copied |
| `src/host/` | The WHATWG HTML Standard's timer and event-loop model, as browsers implement it (from general knowledge; no text was consulted or copied) | The behaviours SP2 mirrors: timers ordered by due time and then creation, a microtask checkpoint after every callback, unhandled rejections reported after the checkpoint, `clearTimeout` and `clearInterval` sharing ids, and extra timer arguments passed to the handler |
| `src/core/json.cpp` | nlohmann-json 3.12.0 `single_include/nlohmann/json.hpp` from the archive in vcpkg's download cache (read 2026-10-12) | nlohmann-json's public API, and that the DOM callback parser reports a member name before storing it and does not catch what the callback throws, which is what the duplicate-name and depth checks rely on; the format of its parse error messages, which `json.cpp` shortens. No nlohmann-json code is copied |
| `src/core/manifest.cpp` | Semantic Versioning 2.0.0 (semver.org; from general knowledge, the page was not consulted) | The version grammar: three numeric identifiers without leading zeros, optional dot-separated pre-release identifiers (numeric ones without leading zeros) and build identifiers. Re-implemented as a small parser; no text or regular expression is copied |
| `src/core/package_files.cpp` | Microsoft's Win32 file naming rules (reserved device names, forbidden characters, trailing dots and spaces dropped), from general knowledge of Microsoft's documentation | The rules `CheckPackagePath` enforces |

## Dependencies fetched at build time

None are stored in this repository. vcpkg downloads them from source archives verified by SHA512.

| Dependency | Pin | Licence | Used by |
|---|---|---|---|
| CommonLibSSE-NG | `ports/commonlibsse-ng`: alandtse/CommonLibSSE-NG tag `v12.1.0` (commit `8ffc2b74af015f837de53cc02b0cb470a15678e4`), SHA512 in the portfile. No patches | GPL-3.0-or-later with Modding Exception and GPL-3.0 Linking Exception | Linked into `SkyrimPlatform2.dll` |
| QuickJS-ng | `ports/quickjs-ng`: quickjs-ng/quickjs tag `v0.17.0` (commit `6d46d07d04041b40f4f49eaa7fdebe44c314c699`), SHA512 in the portfile. No patches | MIT | `sp2_js` (`src/engine/quickjs/`, the `sp2::js` backend), and through it `sp2_host`, `sp2-run` and `sp2_unit_tests`. `SkyrimPlatform2.dll` does not link it yet; it will with the in-game host (M5), and its licence then joins `THIRD_PARTY_NOTICES` |
| spdlog 1.17.0#1, fmt 12.2.0#1 | microsoft/vcpkg registry at baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d` (tag 2026.07.29), `vcpkg-configuration.json` | MIT | Linked into `SkyrimPlatform2.dll` (SP2's log; spdlog is a direct dependency since M1) and used by CommonLibSSE-NG; also the script host's log in `sp2_host` and `sp2-run` |
| nlohmann-json 3.12.0#2 | Same baseline | MIT | Header-only. Package manifests and settings in `sp2_core` (`src/core/json.cpp`, `manifest.cpp`) and `sp2_host`, and through them `sp2-run` and `sp2_unit_tests`. `SkyrimPlatform2.dll` does not use it yet; it will with the in-game package host (M5), and its licence then joins `THIRD_PARTY_NOTICES` |
| DirectXTK 2026-05-07#1, DirectXMath 2026-06-12, rapidcsv 8.99 | Same baseline | MIT; rapidcsv BSD-3-Clause | CommonLibSSE-NG's build; not linked into `SkyrimPlatform2.dll` (linker map, M1) |
| Catch2 3.15.3 | Same baseline | BSL-1.0 | `sp2_unit_tests` only |
| vcpkg-cmake, vcpkg-cmake-config | Same baseline | MIT | Port build helpers |

## Tools used only in CI

| Tool | Pin | Licence |
|---|---|---|
| actions/checkout v7.0.1, actions/cache v5.1.0, actions/setup-python v7.0.0 | Commit SHAs in `.github/workflows/ci.yml` | MIT |
| reuse 6.2.0 and its Python dependencies; poetry-core 2.5.0 to build it | `tools/lint/requirements.txt`, `tools/lint/requirements-build.txt` (sha256 of every PyPI file) | reuse: Apache-2.0 AND CC0-1.0 AND CC-BY-SA-4.0 AND GPL-3.0-or-later; attrs, python-magic, tomlkit, poetry-core: MIT; boolean.py: BSD-2-Clause; click, Jinja2, MarkupSafe: BSD-3-Clause; license-expression: Apache-2.0; python-debian: GPL-2.0-or-later (PyPI metadata, 2026-10-11) |
| vcpkg tool | Bootstrapped from microsoft/vcpkg at the baseline commit, which pins the tool by SHA512 | MIT |
