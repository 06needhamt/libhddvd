# Changelog

Notable changes to libhddvd. Versions follow `project(... VERSION)` in
`CMakeLists.txt`; within a major version the ABI only grows (see
`CLAUDE.md` and `tools/abi_check.sh`). Task IDs such as P7-02 refer to
`docs/IMPLEMENTATION_TASKS.md`.

## 0.6.0 (not yet tagged)

### Licence

- libhddvd is now under the GNU Lesser General Public License, version 2.1
  or later, as libbluray and libdvdnav are. Releases up to 0.5.0 remain
  available under the GPL, version 2.
- `LICENSE` holds the LGPL 2.1 text, the README states the licence, the
  pkg-config file has a `License` field, and every source file names the
  licence with an SPDX identifier.

### Added

- `hddvd-titles --streams` sets a disc's playlist tracks, VTI or title-set
  stream attributes and EVO substreams side by side, to show how
  `streamNumber` maps to substreams on a real disc.
- Player integrations, kept in `contrib/` until they are submitted:
  - `contrib/vlc/`: a VLC 4 access-demux module (P7-02);
  - `contrib/ffmpeg/`: an FFmpeg `hddvd` demuxer (P7-05).

  Each comes with its checks over synthetic discs.

The library's ABI is unchanged from 0.5.0.

## 0.5.0 (2026-11-13, tag v0.5.0)

The version was set to 0.5.0 on 10 November; the tag was made on
13 November, at the merge that also brought the menu sessions, the sector
provider, seeking and the opt-in guesses.

### Added

- Menu sessions (`hddvd_menu`): the object a player's overlay code drives.
  - It loads an HDi application from the disc and joins the script runtime,
    the navigation scheduler, the scene builder and the display list.
  - It takes keys and pointer positions (through the letterbox transform and
    a hit test), and returns a damage-tracked display list and the player
    commands scripts made.
  - `hddvd_menu_seek` handles a player's jump in either direction.
- `hddvd_titles_set_sector_provider`: an authorized host can supply the
  clear form of a scrambled EVO sector. libhddvd itself still decrypts
  nothing.
- `hddvd_std_nav_seek`: Standard Content navigation continues correctly
  after a user seek.
- Opt-in guesses for Standard Content structures whose HD DVD layout is
  unknown, read as DVD-Video lays them out (`hddvd_std_nav_set_options`).
  They are off by default and unconfirmed on any disc:
  - menu and first-play cells, with their buttons and stills
    (`hddvd_std_nav_read_menu`);
  - angle blocks (`hddvd_std_nav_set_angle`);
  - `LinkNextPGC`, `LinkPrevPGC` and `LinkGoUpPGC`.
- Playable titles (`hddvd_titles`, `HDDVD_FEATURE_TITLES`):
  - Standard Content titles come from the title table.
  - Advanced Content titles come from the playlist (`hddvd_open_playlist`,
    `hddvd_titles_open_playlist`). Each primary clip becomes an extent of
    its EVO, found through its time map and VTI.
  - Chapters have byte offsets. A title is read as one MPEG program stream,
    and scrambled packets are refused.
- Standard Content navigation (`hddvd_std_nav`): it follows first play,
  menus, jumps and links through the command VM to the cell to play, with
  libdvdnav's rules. Anything it does not model ends in an explicit step.
- Standard Content tables:
  - title sets (chapters, PGCs, cells, commands, time maps, cell
    addresses, the VOBU map, stream attributes);
  - the VMG title table, menu PGC units and the first-play PGC;
  - a navigation command VM (`hddvd_vm_run`).
- Advanced Content time maps and the VTI EVOB table (`hddvd_tmap`,
  `hddvd_vti`), and title maps from title time to EVO and sector
  (`hddvd_title_map`).
- The on-disc AACS object envelope, with clear image and text payloads.
  Protected content needs a host decryption provider.
- A sandboxed script runtime, with an optional QuickJS engine. It runs disc
  scripts (ECMAScript) within heap, recursion, operation and time budgets,
  and provides:
  - HDi host objects: the document and elements, focus, events, timers,
    animations, `Player` and the application lifecycle;
  - markup timing;
  - a bridge onto the navigation queue.
- Computed XMU styles, navigation and z-order. Display-list scenes are
  built from markup, with fills, text, placed images and image objects.
- Playlist, manifest and markup shapes seen on real discs, and ACA archives
  whose first member follows an unlisted block.
- Tools: `hddvd-titles`, `hddvd-menurun` (with `--state` for trace replay)
  and `hddvd-vtsinfo`.
- Conformance preparation: `tests/replay_reference_trace.py` replays a
  recorded interaction trace, and `docs/REFERENCE_TRACE.md` says how to
  record one.
- Unified configurable limits (`hddvd_config_limits`), with the
  per-subsystem limit structures kept for ABI compatibility.
- Shared and static libraries by default, an installed CMake package and
  pkg-config file, and a consumer test that builds against the installed
  package.
- CI:
  - Linux, macOS and Windows builds;
  - ASan with UBSan, ThreadSanitizer and the Clang static analyzer;
  - a 32-bit build and an ABI check on pull requests;
  - libFuzzer harnesses replayed on every push and fuzzed nightly.

### Fixed

- The `guesses` fixture's first-play cell pointed past the end of its EVO,
  and its test did not read the cell.
- 127 fuzzer-generated inputs had been committed into a seed directory by
  mistake; they are removed.
- A heap over-read in the `HDDVDOBJ` envelope parser, found by the first
  coverage-instrumented fuzzing run.
- A double free in `hddvd-menurun` when a script payload could not be read.
- A NULL dereference in `hddvd_script_close` after a failed allocation,
  and other issues found by the static analyzer.

## 0.4.0 (2026-10-01)

The first published code, GPL-2.0. (The initial upload's README already
called it 0.4.0, while `CMakeLists.txt` said 0.1.0 until 1 October.) It
added:

- HD DVD directory discovery, canonical `file:///dvddisc/` URIs and safe,
  case-insensitive resource lookup;
- refcounted streams;
- a native `HDDVDACA` archive reader with directory and CRC checks;
- XPL playlist, XMF manifest and XMU markup inspection;
- EVO program-stream probing and IFO and MAP header probes;
- foundations for script decoding, renderer-neutral display lists and
  navigation;
- the ACA and menu inspection tools.
