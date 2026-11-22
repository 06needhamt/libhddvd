#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Tom Needham
# Downloads the pinned QuickJS release into DEST, checks its SHA-256, unpacks
# it and prints the source directory to pass as HDDVD_QUICKJS_SOURCE_DIR.
# Keep DEST outside the source tree; the QuickJS source is never committed.
set -eu
version=2026-06-04
sha256=b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a
dest=${1:?usage: fetch_quickjs.sh DEST}

mkdir -p "$dest"
archive="$dest/quickjs-$version.tar.xz"
curl -sSfL --retry 3 -o "$archive" "https://bellard.org/quickjs/quickjs-$version.tar.xz"
if command -v sha256sum >/dev/null 2>&1; then
    echo "$sha256  $archive" | sha256sum -c - >/dev/null
else
    echo "$sha256  $archive" | shasum -a 256 -c - >/dev/null
fi
tar -xJf "$archive" -C "$dest"
echo "$dest/quickjs-$version"
