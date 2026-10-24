#!/bin/sh
# Builds the VLC commit pinned in vlc/VLC_COMMIT with every optional feature
# off (core, libvlc and built-in plugins: enough to build and run the plugin
# test) and installs it into PREFIX. Keep SRC and PREFIX outside the source
# tree. Needs git, meson (>= 1.10), ninja, pkg-config and a C/C++ compiler.
set -eu
src=${1:?usage: build_vlc.sh SRC PREFIX}
prefix=${2:?usage: build_vlc.sh SRC PREFIX}
commit=$(cat "$(dirname "$0")/../vlc/VLC_COMMIT")

if [ ! -d "$src/.git" ]; then
    git init -q "$src"
    git -C "$src" remote add origin https://code.videolan.org/videolan/vlc.git
fi
git -C "$src" fetch -q --depth 1 origin "$commit"
git -C "$src" checkout -q FETCH_HEAD
meson setup "$src/build" "$src" --wipe --prefix="$prefix" --buildtype=debugoptimized \
    -Dauto_features=disabled -Dvlc=false -Dstream_outputs=false -Dvideolan_manager=false \
    -Daddon_manager=false -Dvcd_module=false -Davcodec=disabled -Davformat=disabled \
    -Dpng=disabled -Dzvbi=disabled -Dlua=disabled -Dscreen=disabled -Dswscale=disabled >/dev/null
ninja -C "$src/build" install >/dev/null
