<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Skyrim Underground contributors -->

# Contributing to SkyrimPlatform2

## Developer Certificate of Origin

SkyrimPlatform2 uses the [Developer Certificate of Origin 1.1](https://developercertificate.org/) (DCO) instead of a contributor licence agreement. You keep the copyright in your contribution and license it under the project's licence (AGPL-3.0-or-later with the additional permission in `LICENSE`).

Sign off every commit by committing with `-s`:

```sh
git commit -s -m "feat(core): describe the change"
```

This adds a trailer with the name and email from your git configuration:

```text
Signed-off-by: Your Name <you@example.org>
```

The trailer must match the commit's author name and email. By adding it you certify the DCO for that commit. CI (`tools/lint/dco_check.py`) fails a pull request with any commit that lacks a matching sign-off. To fix older commits on your branch, run `git rebase --signoff <base>` and force-push the branch.

## Before opening a pull request

- Build and test with at least one preset: `cmake --workflow --preset ninja-msvc-dev`
  (see [docs/build.md](docs/build.md)).
- Run the banned-API lint: `python tools/lint/banned_api.py`.
- Run `reuse lint` if you added files.
- Keep one concern per pull request.

## Rules for code

<!-- REUSE-IgnoreStart -->
- Every new file starts with an SPDX header:
  `SPDX-License-Identifier: AGPL-3.0-or-later` and `SPDX-FileCopyrightText: <year> Skyrim Underground contributors` (or your own name), in a comment. Files that cannot hold comments are covered in `REUSE.toml`.
<!-- REUSE-IgnoreEnd -->
- Third-party code keeps its notices and gets a row in `ORIGINS.md` in the same change. Never copy code from SkyrimPlatform's `src/tilted` (All Rights Reserved).
- No hooks, Address Library IDs, network access or process control outside what the design decisions allow; `tools/lint/banned_api.py` lists the rules. Lifting a rule needs a design decision first.
- SP2 writes only under `Documents/My Games/Skyrim Special Edition/SKSE/SkyrimPlatform2/` and logs file names only, never full paths, environment variables or secrets. Update `PRIVACY.md` with any change in behaviour.
- Warnings are errors. Comments are short and single-line, explain why rather than what, and do not narrate the change.
- Nothing speculative: no unused options, placeholder stubs or "todo" markers.
