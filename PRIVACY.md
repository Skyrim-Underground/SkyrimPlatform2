<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Skyrim Underground contributors -->

# Privacy

What SkyrimPlatform2 (SP2) does on the player's computer. This statement covers SP2 version 0.1.0 (milestone M1) and changes in the same commit as any change in behaviour.

## Network

SP2 opens no network connections and listens on no port. It has no HTTP client, update check, telemetry, analytics or crash upload. A source lint (`tools/lint/banned_api.py`) fails the build if network APIs or network libraries appear in SP2's code.

## Files SP2 writes

SP2 writes only inside one folder:

`Documents\My Games\Skyrim Special Edition\SKSE\SkyrimPlatform2\`

"Documents" is your Windows Documents folder, which Windows may have moved (for example into OneDrive).

`SkyrimPlatform2.log` 
Started anew each time the game starts with SP2 installed. 
Time-stamped lines: SP2's version and build type; the game version, the SKSE version and the Address Library file name (for example `versionlib-1-7-104-0.bin`) and whether that file is present; whether the version check passed; when SKSE reported that plugins and game data finished loading

`SkyrimPlatform2.1.log` to `SkyrimPlatform2.3.log`
When a new log starts, or a log reaches 5 MB
The three previous logs; older ones are deleted

The log contains no paths, user or computer names, hardware identifiers, environment variables or settings. 
As a safeguard, SP2 replaces your user profile folder (for example `C:\Users\<name>`) with `%USERPROFILE%` in every log line, including messages from libraries SP2 uses. 

SP2 writes nothing else on disk and nothing to the Windows registry.

## Files SP2 reads

- The version information of the running game executable (`SkyrimSE.exe`), to check the game version.
- Whether the Address Library file for that version exists in `Data\SKSE\Plugins\`. SP2 does not open it.
- The locations of your Documents and user profile folders, from Windows. The profile location is used only to remove it from log lines.

Windows loads `SkyrimPlatform2.dll`, and SKSE reads the version information SP2 declares.

## What you may see

If the game or SKSE version is not supported, SP2 shows one message box saying so and does not start. SKSE then shows its own message about a plugin that did not load and offers to exit the game; if you decline, the game runs without SP2.

## What SP2 never does

- Collect hardware or machine identifiers, process lists or window titles.
- Poll the keyboard outside the game's own input events.
- Start, stop or inspect other processes, or force a window to the foreground.

## How to check

1. Delete the `SkyrimPlatform2` folder above and start the game. Only that folder and its log come back.
2. Run a network monitor of your choice while the game runs; SP2 adds no connections.
3. Read `tools/lint/banned_api.py` for the list of APIs SP2's code may not use.

## Other software

SKSE, Address Library and other mods behave independently of SP2. For example, SKSE writes its own `skse64.log` in the `SKSE` folder. This statement covers SP2 only.
