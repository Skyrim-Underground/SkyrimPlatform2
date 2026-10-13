#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Skyrim Underground contributors
"""Checks that every non-merge commit in BASE..HEAD has a DCO sign-off by its author.

A commit passes when its message has a "Signed-off-by: Name <email>" trailer whose name equals the commit author's name and whose email equals the author's email (compared case-insensitively), which is what `git commit -s` writes. 

See CONTRIBUTING.md.

Usage: python tools/lint/dco_check.py BASE HEAD
"""

from __future__ import annotations

import re
import subprocess
import sys

SIGNOFF = re.compile(r"^Signed-off-by:\s*(?P<name>.+?)\s*<(?P<email>[^<>]+)>\s*$", re.MULTILINE)


def is_signed_off(author_name: str, author_email: str, message: str) -> bool:
    return any(
        match.group("name") == author_name.strip() and match.group("email").lower() == author_email.strip().lower()
        for match in SIGNOFF.finditer(message)
    )


def git(*args: str) -> str:
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True, encoding="utf-8").stdout


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    base, head = sys.argv[1], sys.argv[2]

    commits = git("rev-list", "--no-merges", "--reverse", f"{base}..{head}").split()
    unsigned = []
    for commit in commits:
        name, email, message = git("show", "-s", "--format=%an%x00%ae%x00%B", commit).split("\0", 2)
        if not is_signed_off(name, email, message):
            unsigned.append(f"{commit[:12]} {name} <{email}>: {message.splitlines()[0] if message.strip() else ''}")

    for line in unsigned:
        print(f"missing DCO sign-off: {line}")
    print(f"DCO check: {len(commits)} commits, {len(unsigned)} without a matching Signed-off-by")
    return 1 if unsigned else 0


if __name__ == "__main__":
    sys.exit(main())
