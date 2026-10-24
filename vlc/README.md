# VLC plugin

`hddvd.c` is a VLC 4 access module that plays HD DVD Standard Content titles
through libhddvd (P7-02 in `docs/IMPLEMENTATION_TASKS.md`). It lives here
while libhddvd's API settles. The goal is to submit it to VLC's own tree,
next to `modules/access/bluray.c`.

## What it does

- **Opening:** `hddvd:///path/to/disc` opens a disc root, the directory that
  holds `HVDVD_TS`. Opening that directory as an ordinary file or folder works
  too.
- **Titles and chapters:** the plugin lists the disc's titles and chapters
  from `hddvd_titles_open`, with their durations and chapter times. VLC's
  title and chapter menus and its `@title:chapter` MRL syntax work as for
  other discs.
- **Playback:** a title plays as one byte stream of its cells' EVO sectors
  (`hddvd_titles_read`). VLC's MPEG-PS demuxer reads that stream, and passes
  title and chapter changes back to the plugin, as it does for VCDs.
- **Encrypted titles:** AACS-protected titles stop with an "Encrypted HD DVD"
  error dialog. libhddvd does not decrypt; `docs/formats/ifo-map.md` explains
  how encrypted sectors are recognised.

Not yet supported:
- Advanced Content (HDi) titles and menus, so most commercial discs, which
  are Advanced Content only, do not open yet.
- Standard Content menus and navigation commands. Titles play straight
  through.
- Angle blocks.

## Building

The plugin needs an installed VLC 4. CI uses the `master` commit pinned in
`VLC_COMMIT`. `tools/build_vlc.sh` builds that commit with every optional
feature off, which is enough to build and test the plugin. It takes about a
minute.

```sh
tools/build_vlc.sh /tmp/vlc-src /tmp/vlc
export PKG_CONFIG_PATH=$(echo /tmp/vlc/lib/*/pkgconfig)
cmake -S . -B build -DBUILD_TESTING=ON -DHDDVD_BUILD_VLC_PLUGIN=ON -DCMAKE_PREFIX_PATH=/tmp/vlc
cmake --build build
ctest --test-dir build -R vlc-plugin --output-on-failure
```

- **Locating VLC:** CMake uses the `vlc-plugin` pkg-config package where VLC
  provides one (autotools installs). Otherwise it looks for the plugin
  headers and libvlccore under `CMAKE_PREFIX_PATH`, because meson installs
  ship no `vlc-plugin.pc`.
- **Output:** the plugin is built as `build/vlc/plugins/libhddvd_plugin.so`,
  with libhddvd linked in statically and its symbols hidden.
- **Installing:** `HDDVD_VLC_PLUGIN_DIR` sets where it installs. The default
  is VLC's `plugins/access` directory.
- **Without installing:** point VLC at the build directory with
  `VLC_PLUGIN_PATH=build/vlc/plugins`.

**The test (`test_plugin.c`)** runs the plugin in a headless libvlc over the
synthetic disc in `tests/fixtures/standard`. It checks:
- the title durations and chapter times;
- that chapter and title changes move the plugin to the expected byte
  offsets (read from its debug messages);
- that a title plays to its end;
- that a copy with a scrambled packet raises the encryption dialog;
- that a plain directory open finds the disc;
- that a directory without a title table is refused.

## Bumping VLC

VLC's `master` API changes; the libvlc 4 calls in the test have changed
during 4.0 development. To move to a newer VLC:
1. Update `VLC_COMMIT`. The CI cache key includes it, so VLC is rebuilt.
2. Fix whatever no longer compiles, in the same change.

## Before submitting upstream

- **License notice:** VLC modules carry a license notice in their header.
  Decide whether libhddvd and this module are GPLv2 only or GPLv2 or later,
  and add the matching notice.
- **Build files:** add the module to `modules/access/meson.build` and
  `Makefile.am`, depending on a `libhddvd` dependency.
- **Contrib:** add libhddvd to `contrib/src` from a tagged release tarball.
- **Fallback macros:** in VLC's tree, `config.h` defines `_()` and `N_()`.
  The fallbacks in `hddvd.c` exist only for the out-of-tree build and can go.
- **Development branch:** submit against VLC's `master`. New modules are not
  added to the 3.0 branch.
