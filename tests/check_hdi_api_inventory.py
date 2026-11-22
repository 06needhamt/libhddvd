#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Tom Needham
"""Run tools/inventory_hdi_scripts.py on the original fixtures and compare its
whole report with the expected one."""

from pathlib import Path
import subprocess
import sys


def main():
    if len(sys.argv) != 4:
        print("usage: check_hdi_api_inventory.py TOOL FIXTURE_DIR EXPECTED", file=sys.stderr)
        return 2
    tool, fixtures, expected = sys.argv[1:]
    result = subprocess.run(
        [sys.executable, "-I", tool, fixtures], capture_output=True, text=True, check=False
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        return 1
    want = Path(expected).read_text(encoding="utf-8")
    if result.stdout != want:
        print("report differs from %s:\n%s" % (expected, result.stdout), file=sys.stderr)
        return 1
    # A missing source is an error, not an empty report.
    missing = subprocess.run(
        [sys.executable, "-I", tool, str(Path(fixtures) / "missing")],
        capture_output=True,
        check=False,
    )
    return 0 if missing.returncode != 0 else 1


if __name__ == "__main__":
    sys.exit(main())
