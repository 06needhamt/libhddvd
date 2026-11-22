#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Tom Needham
"""Validate the opt-in, externally supplied reference menu."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


EXPECTED_SIZE = 3_488_246
EXPECTED_SHA256 = "de89cf076bcebe005c4e0d93dcf9f97c18c196fe5cb6fcac83069857f165d699"


def fail(message):
    print(f"reference-menu: {message}", file=sys.stderr)
    return 1


def main():
    if len(sys.argv) != 3:
        return fail("internal error: expected acainfo and inventory arguments")

    archive_name = os.environ.get("HDDVD_REFERENCE_ACA")
    if not archive_name:
        print("reference-menu: HDDVD_REFERENCE_ACA is not set; skipping")
        return 77

    archive = Path(archive_name)
    if not archive.is_file():
        return fail(f"HDDVD_REFERENCE_ACA is not a file: {archive}")
    data = archive.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != EXPECTED_SIZE:
        return fail(f"size is {len(data)}, expected {EXPECTED_SIZE}")
    if digest != EXPECTED_SHA256:
        return fail(f"SHA-256 is {digest}, expected {EXPECTED_SHA256}")

    expected = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
    process = subprocess.run(
        [sys.argv[1], "--json", str(archive)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if process.returncode:
        sys.stderr.write(process.stderr)
        return fail(f"hddvd-acainfo exited with {process.returncode}")
    try:
        inventory = json.loads(process.stdout)
    except json.JSONDecodeError as error:
        return fail(f"hddvd-acainfo produced invalid JSON: {error}")
    if inventory != expected:
        return fail("ACA inventory differs from tests/reference_menu_expected.json")
    if inventory["entry_count"] != 40 or not inventory["crc_ok"]:
        return fail("expected 40 members with valid CRCs")

    report = {
        "schema": 1,
        "size": len(data),
        "sha256": digest,
        "inventory": inventory,
    }
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    report_name = os.environ.get("HDDVD_REFERENCE_REPORT")
    if report_name:
        Path(report_name).write_text(rendered, encoding="utf-8")
    sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    sys.exit(main())
