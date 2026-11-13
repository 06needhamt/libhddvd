# libhddvd 0.5.0

Experimental open-source HD DVD inspection library.

## Implemented

- HD DVD directory discovery (standard and advanced content)
- XPL XML inspection
- Native `HDDVDACA` reader (no libzip dependency)
- ACA directory listing, bounds validation, CRC-32 verification, member reads and safe extraction
- XMF manifest and XMU markup inspection
- Strict versioned object envelopes, opaque trailer metadata, bounded payload
  streams, and explicit protected-content provider handling
- Immutable typed XMF manifests with validated application metadata, regions,
  ordered script/markup references, resource roots, and configurable limits
- EVO program-stream probing
- IFO/VTI/MAP header validation, the VTI's EVOB table, and time maps; a title
  time resolves through the playlist's clips to an EVO file and the sector to
  read from
- Standard Content title sets and title tables: chapters, PGCs, cells, raw
  navigation commands, time maps, cell addresses, VOBU maps and stream
  attributes (video coding, audio and subpicture languages), menu PGCs and
  the first-play PGC (`hddvd_vts_open`, `hddvd_vmg_open`), and a navigation
  command VM (`hddvd_vm_run`); VTI attribute records
- Playable titles (`hddvd_titles`): Standard Content titles from the title
  table (`hddvd_titles_open`) and Advanced Content titles from the playlist
  (`hddvd_open_playlist`, `hddvd_titles_open_playlist`), with chapters and
  durations, and a title's EVO sectors read as one MPEG program stream, with
  encrypted (AACS) sectors refused as `HDDVD_EPROTECTED`
- Standard Content navigation (`hddvd_std_nav`): first play, menus, jumps and
  links followed through the command VM to the cell to play, plus the user's
  title, chapter and menu choices. Opt-in guesses (`hddvd_std_nav_set_options`)
  read DVD-Video's layout where HD DVD's is unknown, for menu cells and their
  buttons, angle blocks and PGC links; no disc has confirmed them
- A host provider hook for protected title sectors
  (`hddvd_titles_set_sector_provider`); libhddvd itself decrypts nothing
- Menu sessions (`hddvd_menu`): one HDi application loaded from the disc and
  run for a player's overlay code, which sends keys, pointer positions, time
  and player events and reads back a damage-tracked display list and the
  player commands scripts made
- A sandboxed script runtime (`hddvd_runtime`) that runs HDi ECMAScript source,
  UTF-16 as on disc or UTF-8, with heap, recursion, operation and wall-clock
  budgets, in an optional QuickJS engine. Scripts get the HDi `document`
  (elements with their state and style), `application` events, listeners,
  timers and animations; the host loads markup, sends keys and time, and reads
  back the focus and the styles with script values applied, for
  `hddvd_scene_build`. `Player` reads the playlist and the player state the
  host reports, and turns playback calls into typed commands the host takes
  and acknowledges; file, XML, network and storage objects are denied, and
  unsupported members throw. Markup timing runs: `par`/`cue` containers,
  `set`, `animate` and `event` actions and the XPath subset discs use.
  `hddvd_runtime_bridge` runs a runtime as tasks
  on the navigation queue, so scripts see input, time, player events and
  command answers in queue order. `docs/hdi-host-api.md` lists what disc scripts
  use. A bounded structural decoder covers the clean-room `HDIS` fixture
  format only
- Renderer-neutral, damage-tracked display lists with host-decoded premultiplied
  RGBA planes, nested clipping/layout, stable stacking, focus image selection,
  letterbox transforms, and reverse-paint-order hit testing, built directly from
  markup and its computed styles with `hddvd_scene_build`

## ACA tools

```sh
hddvd-acainfo archived.aca
hddvd-acaextract archived.aca extracted
hddvd-menuinfo archived.aca
hddvd-menuinfo --scene archived.aca MARKUP.xmu [FOCUSED_ID]
```

`--scene` lays out one markup file the way the library would draw it:
- every scene node, with its bounds, z-order, opacity, visibility and images;
- every display-list item (images, background fills and text runs), with its destination and clip.

Images are resolved by name among the archive's members. This makes it easy to compare a menu's initial state against a real player.

`hddvd-xplinfo playlist.xpl` summarizes the typed playlist model as JSON and
lists every construct it parsed but does not model. `hddvd-scriptinfo FILE`
accounts for every byte of a script object as documented structure, observed
but unconfirmed bytes, or unknown ranges (see `docs/formats/hdi-script.md`).
`hddvd-timemap HVA00001.VTI FILE.MAP...` lists the EVOBs a VTI describes and
checks each time map against them (see `docs/formats/ifo-map.md`).
`hddvd-locate DISC_ROOT PLAYLIST.XPL TITLE_INDEX MILLISECONDS...` resolves title
times to the EVO file, presentation time and sector that play them.
`hddvd-vtsinfo HVnnnI01.IFO [HVnnnT01.EVO]` prints a Standard Content title
set or title table, its stream attributes, menus and navigation commands (in
DVD-Video syntax), and checks its tables and every command target; given the
title EVO, it checks them against the EVO's navigation packs and streams. For
a title table it also runs the first-play commands and prints where they lead.
`hddvd-titles [--read] [--navigate COUNT [--guess]] [--streams [--scan MIB]] DISC_ROOT` lists a disc's playable
titles, Standard Content and Advanced Content, with their chapters and the EVO
extents they play. `--read` reads every title through and says whether its
data is clear, encrypted or damaged; `--navigate` follows a Standard Content
disc's own commands from first play for up to COUNT cells, and `--guess` turns
the navigator's guesses on, listing menu cells with their buttons and pressing
the selected one. `--streams` sets a disc's three accounts of its audio and
subpicture streams side by side: each playlist title's tracks (streamNumber,
language, codec), the audio streams of its clips' VTI attribute record, and
the stream and substream ids in the clips' EVO packet headers (Standard
Content: each title set's stream attributes and its title EVO's streams).
Packet headers are clear on AACS discs, so encrypted discs can be probed;
packets marked scrambled are counted apart. Each EVO is scanned for `--scan`
MiB (256 by default, 0 for all of it). Its output describes a real disc, so
it is for the user's own reference and is not to be committed.
`hddvd-menurun [--state] [--before FILE] [--playlist FILE.xpl [--title ID]] archive.aca
MANIFEST.xmf [STEP...]` runs one application in the script runtime: its
manifest's scripts, its markup and the markup loaded handler, then steps such
as `right`, `enter`, `key:0xCE`, `wait:1.5` or `event:TYPE`, printing the
focus after each and every script failure and player command. With a
playlist it reports the title that runs the application (or `--title`) and
its application list; `--before` first runs a local script.
`python3 tools/inventory_hdi_scripts.py DIR...` lists the host objects, methods
and events that the `.js` files under each directory use, without printing any
source (see `docs/hdi-host-api.md`).

The ACA implementation was validated against a genuine HD DVD archive with 40 members. The sample itself is not distributed with this source tree. Regression tests construct a small synthetic archive using the observed container structure.

## Resource APIs

Phase 1 provides checked 64-bit file and memory streams, persistent ACA handles
with bounded member streams, canonical `file:///dvddisc/...` URI parsing, and
case-insensitive resource lookup through `hddvd_open_resource`. ACA member streams
retain their backing archive stream and remain valid after the archive handle is
closed. Resource lookup rejects traversal, encoded separators, ambiguous
case-insensitive matches, and (on POSIX systems) symbolic links.

## Build

Requires libxml2.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The script runtime needs an ECMAScript engine; without one, libhddvd builds
and reports no `HDDVD_FEATURE_SCRIPT_RUNTIME`. To build QuickJS into libhddvd,
unpack a QuickJS release (2021-03-27 or later, from https://bellard.org/quickjs/)
outside the source tree and point the build at it:

```sh
QUICKJS=$(tools/fetch_quickjs.sh /tmp/quickjs)   # pinned release, SHA-256 checked
cmake -S . -B build -DBUILD_TESTING=ON -DHDDVD_SCRIPT_ENGINE=quickjs \
      -DHDDVD_QUICKJS_SOURCE_DIR="$QUICKJS"
```

`HDDVD_SCRIPT_ENGINE` is `auto` (the default: QuickJS when available), `quickjs`
(fail if it is not) or `none`. Without a source directory, an installed
`libquickjs.a` is used; distribution packages are usually not built with
`-fPIC`, so only static-only builds (`-DHDDVD_BUILD_SHARED=OFF`) can use them.
QuickJS does not build with MSVC. The choice is recorded in
`docs/decisions/0001-script-engine.md`.

Both shared and static libraries are built by default. Use
`-DHDDVD_BUILD_SHARED=OFF` or `-DHDDVD_BUILD_STATIC=OFF` to build only one
variant. Configuration fails if both options are disabled. The primary
`hddvd::hddvd` target selects the shared library when it is available, while
`hddvd::hddvd_static` explicitly selects the static library when both variants
are enabled.

All C and C++ sources use Allman braces and are formatted with the checked-in
ClangFormat configuration. Run the following before submitting changes:

```sh
find include src tests tools -type f \
  \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) -print0 | \
  xargs -0 clang-format -i
```

## Integration and capabilities

Installed builds provide both a `libhddvd.pc` pkg-config file and a CMake config
package exporting the `hddvd::hddvd` target.  Consumers can query
`hddvd_abi_version()` and `hddvd_get_features()` before enabling individual
integration paths.  Feature bits deliberately distinguish the available script
decoder and host-driven image display list from the not-yet-available built-in
image decoder; `HDDVD_FEATURE_SCRIPT_RUNTIME` is set only in builds with an
engine (`hddvd_runtime_engine()` names it). No feature bit implies complete HDi
menu conformance.

```cmake
find_package(hddvd 0.5 CONFIG REQUIRED)
target_link_libraries(my_player PRIVATE hddvd::hddvd)
```

The VLC access/demux and overlay modules are developed in the VLC source
tree, as VLC's modules for libdvdnav and libbluray are. This library supplies
the versioned ABI, capability negotiation, playable titles (`hddvd_titles`),
Standard Content navigation (`hddvd_std_nav`),
renderer-neutral display list, and serialized navigation foundations those
modules consume.

Context-bound resource failures are available through
`hddvd_get_last_error()`, which reports a stable status, subsystem, resource URI,
optional byte offset, and explanatory message. A later successful resource
operation clears the prior diagnostic so hosts do not display stale errors.
Context-wide ceilings are configured with `hddvd_config_limits_default()` and
`hddvd_open_with_limits()`. The same object can be passed to the additive
`*_with_config` constructors; the original subsystem-specific structures and
entry points remain available and ABI-compatible.

## Reference-menu conformance test

The HDi reference archive is deliberately not distributed or downloaded by the
project.  To validate a separately obtained copy, enable the opt-in test and pass
its path at test time:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DHDDVD_ENABLE_REFERENCE_TESTS=ON
cmake --build build
HDDVD_REFERENCE_ACA=/outside/the/repository/reference.aca \
  ctest --test-dir build -R reference-menu --output-on-failure
```

The test checks the fixed size and SHA-256 identity, the complete member
inventory, and every member CRC.  Set `HDDVD_REFERENCE_REPORT` to write the JSON
report to a chosen path.  With no `HDDVD_REFERENCE_ACA`, CTest reports the test as
skipped.  The same option adds `reference-trace`, which replays an interaction
trace recorded on a player (`HDDVD_REFERENCE_TRACE`) against libhddvd; how to
record and replay one is in `docs/REFERENCE_TRACE.md`.
See `docs/HDI_MENU_IMPLEMENTATION_PLAN.md` for the remaining phases.
Reviewable tasks, dependencies, and acceptance criteria for those phases are in
`docs/IMPLEMENTATION_TASKS.md`.

Additional externally supplied menu corpora can be inventoried without copying
assets into the repository. Enable `HDDVD_ENABLE_EXTERNAL_MENU_TESTS`, set
`HDDVD_EXTERNAL_MENU_CORPUS` to a ZIP or extracted directory, and optionally set
`HDDVD_EXTERNAL_MENU_REPORT` to a JSON output path. Set
`HDDVD_EXTERNAL_MENU_STRICT=1` to also fail when libhddvd rejects a playlist or
leaves any of its constructs unmodeled. See
`docs/EXTERNAL_MENU_INVENTORY.md` for the external-only data policy and report
schema.

Parser fuzz harnesses can be built with Clang and
`-DHDDVD_BUILD_FUZZERS=ON`; the checked-in inputs under `tests/fixtures` are
original synthetic seeds and contain no bytes from the reference archive.

## Status

This remains experimental software. ACA parsing is now based on observed media and strongly validated by offsets and CRCs. Standard Content navigation follows DVD-Video's rules and has not been checked against a player; menu cells, angle blocks and conformance against a real player's behavior are future work.
