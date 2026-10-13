#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors
"""Fails when SP2's own sources use an API that SP2 must not use.

Every C++ and CMake file is scanned as text, comments and string literals included, so a name passed to GetProcAddress is caught too. 
There is no inline suppression: lifting a rule means changing this file.

Usage: python tools/lint/banned_api.py
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CXX_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inl", ".ipp", ".rc"}
CMAKE_NAMES = {"CMakeLists.txt"}
CMAKE_SUFFIXES = {".cmake"}
SKIPPED_DIRS = {".git", "bin", "out", "build", "vcpkg_installed", "node_modules"}


@dataclass(frozen=True)
class Rule:
    name: str
    pattern: re.Pattern[str]
    reason: str


def _words(*names: str) -> re.Pattern[str]:
    return re.compile(r"\b(?:" + "|".join(names) + r")\b")


CXX_RULES = [
    Rule(
        "network",
        _words(
            r"WSAStartup", r"WSASocket\w*", r"getaddrinfo", r"GetAddrInfo\w*", r"gethostbyname",
            r"InternetOpen\w*", r"InternetConnect\w*", r"HttpOpenRequest\w*", r"HttpSendRequest\w*",
            r"WinHttp\w+", r"URLDownloadTo\w+", r"URLOpen\w*Stream\w*",
        ),
        "SP2 opens no network connections",
    ),
    Rule(
        "network-header",
        re.compile(r"#\s*include\s*[<\"](?:winsock2?|ws2tcpip|wininet|winhttp|urlmon|curl/curl|httplib)\.h[>\"]", re.IGNORECASE),
        "SP2 opens no network connections",
    ),
    Rule(
        "machine-identifier",
        _words(
            r"GetComputerName\w*", r"GetUserName\w*", r"GetVolumeInformation\w*", r"GetAdaptersInfo",
            r"GetAdaptersAddresses", r"GetSystemFirmwareTable", r"EnumSystemFirmwareTables", r"MachineGuid",
        ),
        "SP2 reads no hardware or machine identifiers",
    ),
    Rule(
        "process-or-window-inspection",
        _words(
            r"CreateToolhelp32Snapshot", r"Process32First\w*", r"Process32Next\w*", r"EnumProcesses",
            r"EnumWindows", r"GetForegroundWindow", r"GetWindowText\w*",
        ),
        "SP2 never lists processes, reads window titles or tracks the foreground window",
    ),
    Rule(
        "key-polling",
        _words(r"GetAsyncKeyState", r"GetKeyboardState", r"GetKeyState"),
        "input comes from the game's input events, never from key polling",
    ),
    Rule(
        "process-control",
        re.compile(
            r"\b(?:TerminateProcess|OpenProcess|SetForegroundWindow|BringWindowToTop|AllowSetForegroundWindow"
            r"|CreateProcess\w*|ShellExecute\w*|WinExec)\b|\b(?:_w?popen|system)\s*\("
        ),
        "SP2 starts no processes, runs no shell commands and never takes focus",
    ),
    Rule(
        "hook",
        _words(
            r"MH_\w+", r"Detour\w+", r"VirtualProtect\w*", r"write_vfunc", r"write_branch", r"write_call",
            r"safe_write", r"safe_fill", r"AllocTrampoline", r"GetTrampoline", r"Trampoline",
        ),
        "hooks come only from the hook table, once the binding layer needs one",
    ),
    Rule(
        "relocation",
        re.compile(r"\bREL::(?:ID|Offset|Relocation|RelocationID|VariantID|VariantOffset)\b|\b(?:RELOCATION_ID|VARIANT_OFFSET)\b"),
        "Address Library IDs belong in the relocation registry, which arrives with the binding layer",
    ),
    Rule(
        "dll-loading",
        _words(r"LoadLibrary", r"LoadLibraryA", r"LoadLibraryW", r"LoadLibraryExA", r"LdrLoadDll", r"WinVerifyTrust"),
        "DLLs load only by full path through LoadLibraryExW with safe search flags, and never through WinVerifyTrust",
    ),
    Rule(
        "environment",
        _words(
            r"getenv", r"_wgetenv", r"_dupenv_s", r"_wdupenv_s", r"GetEnvironmentVariable\w*",
            r"GetEnvironmentStrings\w*", r"ExpandEnvironmentStrings\w*",
        ),
        "SP2 reads no environment variables and never logs PATH or the environment",
    ),
    Rule(
        "registry-write",
        _words(
            r"RegSetValue\w*", r"RegSetKeyValue\w*", r"RegCreateKey\w*", r"RegDeleteKey\w*",
            r"RegDeleteValue\w*", r"RegDeleteTree\w*", r"SHSetValue\w*",
        ),
        "SP2 writes only under its own My Games folder",
    ),
]

CMAKE_RULES = [
    Rule(
        "network-library",
        re.compile(r"\b(?:ws2_32|wsock32|wininet|winhttp|urlmon|iphlpapi|dnsapi|wbemuuid)(?:\.lib)?\b", re.IGNORECASE),
        "SP2 links no network or system-inventory library",
    ),
]


def file_kind(path: Path) -> str | None:
    """'cxx', 'cmake' or None. A configured template such as build_info.h.in counts as the file it produces."""
    suffix = Path(path.stem).suffix if path.suffix == ".in" else path.suffix
    if suffix in CXX_SUFFIXES:
        return "cxx"
    if suffix in CMAKE_SUFFIXES or path.name in CMAKE_NAMES:
        return "cmake"
    return None


def scan(path: Path) -> list[str]:
    rules = CXX_RULES if file_kind(path) == "cxx" else CMAKE_RULES
    shown = path.relative_to(ROOT).as_posix()
    findings = []
    for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), start=1):
        for rule in rules:
            for match in rule.pattern.finditer(line):
                findings.append(f"{shown}:{number}: {rule.name}: '{match.group(0)}': {rule.reason}")
    return findings


def main() -> int:
    files = sorted(
        path
        for path in ROOT.rglob("*")
        if path.is_file() and file_kind(path) and not any(part in SKIPPED_DIRS for part in path.relative_to(ROOT).parts[:-1])
    )
    findings = [finding for path in files for finding in scan(path)]
    for finding in findings:
        print(finding)
    print(f"banned-API lint: {len(files)} files checked, {len(findings)} findings")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
