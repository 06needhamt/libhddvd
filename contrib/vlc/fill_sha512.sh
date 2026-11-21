#!/bin/sh
# After the v0.6.0 tag exists on GitHub: downloads its tarball, writes
# contrib/src/hddvd/SHA512SUMS into the contrib commit (the first of the
# three), and re-exports the patches. Usage: fill_sha512.sh VLC_TREE OUT_DIR
set -eu
vlc=$1 out=$2
tmp=$(mktemp -d)
if [ -n "${TARBALL:-}" ]; then
    cp "$TARBALL" "$tmp/libhddvd-0.6.0.tar.gz"  # for testing only
else
    curl -fsSL -o "$tmp/libhddvd-0.6.0.tar.gz" \
        https://github.com/06needhamt/libhddvd/archive/refs/tags/v0.6.0.tar.gz
fi
sum=$(sha512sum "$tmp/libhddvd-0.6.0.tar.gz" | cut -d' ' -f1)
cd "$vlc"
base=$(git rev-parse HEAD~3)
contrib=$(git rev-parse HEAD~2)
git stash -q -u || true
git checkout -q -b fill-sha512 "$contrib"
printf '%s  libhddvd-0.6.0.tar.gz\n' "$sum" > contrib/src/hddvd/SHA512SUMS
git add contrib/src/hddvd/SHA512SUMS
git commit -q --amend --no-edit
git cherry-pick "$contrib"..hddvd >/dev/null
git branch -f hddvd HEAD && git checkout -q hddvd && git branch -D fill-sha512 >/dev/null
rm -f "$out"/*.patch
git format-patch -q "$base"..hddvd -o "$out"
echo "SHA512 $sum"
