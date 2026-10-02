#!/usr/bin/env python3
"""Run the external-only menu inventory as an opt-in CTest."""

import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    if len(sys.argv) != 3:
        print("external-menu-corpus: expected inventory tool and acaextract", file=sys.stderr)
        return 1
    corpus = os.environ.get("HDDVD_EXTERNAL_MENU_CORPUS")
    if not corpus:
        print("external-menu-corpus: HDDVD_EXTERNAL_MENU_CORPUS is not set; skipping")
        return 77
    process = subprocess.run(
        [sys.executable, sys.argv[1], corpus, sys.argv[2]],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if process.returncode:
        sys.stderr.write(process.stderr)
        return process.returncode
    report = json.loads(process.stdout)
    observations = report["observations"]
    if not observations["xpl"]["documents"]:
        print("external-menu-corpus: no parseable XPL documents", file=sys.stderr)
        return 1
    if not observations["xmf_xmu_families"]:
        print("external-menu-corpus: no parseable XMF/XMU families", file=sys.stderr)
        return 1
    report_path = os.environ.get("HDDVD_EXTERNAL_MENU_REPORT")
    if report_path:
        Path(report_path).write_text(process.stdout, encoding="utf-8")
    print(process.stdout, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
