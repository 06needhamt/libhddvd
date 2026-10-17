#!/bin/sh
# Compare two builds of the shared library and fail on anything but
# additions: a removed or changed function or variable (which includes a
# public struct changing size or layout) breaks the ABI rule in CLAUDE.md.
# abidiff's own "incompatible" exit bit is not enough on its own: it stays
# clear when a struct reachable from an existing function changes layout.
#
# usage: abi_check.sh OLD.so OLD_INCLUDE_DIR NEW.so NEW_INCLUDE_DIR
set -u
if [ $# -ne 4 ]; then
    echo "usage: $0 OLD.so OLD_INCLUDE_DIR NEW.so NEW_INCLUDE_DIR" >&2
    exit 2
fi
report=$(abidiff --headers-dir1 "$2" --headers-dir2 "$4" "$1" "$3")
status=$?
printf '%s\n' "$report"
# Bits 1 and 2 mean abidiff itself failed; bit 8 an incompatible change.
if [ $((status & 11)) -ne 0 ]; then
    echo "abi_check: abidiff exit status $status" >&2
    exit 1
fi
if printf '%s\n' "$report" | grep -Eq 'changes summary: ([1-9][0-9]* Removed|0 Removed, [1-9][0-9]* Changed)'; then
    echo "abi_check: functions or variables were removed or changed" >&2
    exit 1
fi
echo "abi_check: only additions"
