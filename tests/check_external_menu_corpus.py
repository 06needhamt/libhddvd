#!/usr/bin/env python3
"""Run the external-only menu inventory as an opt-in CTest."""

import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    if len(sys.argv) != 4:
        print(
            "external-menu-corpus: expected inventory tool, acaextract and xplinfo",
            file=sys.stderr,
        )
        return 1
    corpus = os.environ.get("HDDVD_EXTERNAL_MENU_CORPUS")
    if not corpus:
        print("external-menu-corpus: HDDVD_EXTERNAL_MENU_CORPUS is not set; skipping")
        return 77
    command = [sys.executable, sys.argv[1], corpus, sys.argv[2], "--xplinfo", sys.argv[3]]
    # Strict mode is the P6-02 conformance gate: every playlist must parse and
    # every construct in it must be represented by the typed model.
    if os.environ.get("HDDVD_EXTERNAL_MENU_STRICT"):
        command.append("--require-modeled")
    process = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    report_path = os.environ.get("HDDVD_EXTERNAL_MENU_REPORT")
    # Keep the report even when strict mode fails; it names what is missing.
    if report_path and process.stdout:
        Path(report_path).write_text(process.stdout, encoding="utf-8")
    if process.returncode:
        sys.stderr.write(process.stderr)
        return process.returncode
    report = json.loads(process.stdout)
    observations = report["observations"]
    if not observations["xpl"]["documents"]:
        print("external-menu-corpus: no parseable XPL documents", file=sys.stderr)
        return 1
    if observations["xpl"]["library"]["failed"]:
        print("external-menu-corpus: libhddvd rejected playlists: "
              + ", ".join(observations["xpl"]["library"]["failed"]), file=sys.stderr)
        return 1
    if not observations["xmf_xmu_families"]:
        print("external-menu-corpus: no parseable XMF/XMU families", file=sys.stderr)
        return 1
    print(process.stdout, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
