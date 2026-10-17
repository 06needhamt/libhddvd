# libhddvd 0.4.0

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
- Standard Content title sets and title tables: chapters, PGCs, cells, time
  maps, cell addresses and VOBU maps (`hddvd_vts_open`, `hddvd_vmg_open`)
- A sandboxed script runtime (`hddvd_runtime`) that runs HDi ECMAScript source,
  UTF-16 as on disc or UTF-8, with heap, recursion, operation and wall-clock
  budgets, in an optional QuickJS engine. HDi host objects (`Player`,
  `document`, events, timers) are not provided yet; `docs/hdi-host-api.md`
  lists the ones disc scripts use. A bounded structural decoder covers the
  clean-room `HDIS` fixture format only
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
set or title table and checks its tables; given the title EVO, it checks them
against the EVO's navigation packs.
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
find_package(hddvd 0.4 CONFIG REQUIRED)
target_link_libraries(my_player PRIVATE hddvd::hddvd)
```

The VLC access/demux and overlay modules remain downstream work in the VLC
source tree.  This release supplies the versioned ABI, capability negotiation,
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
skipped.  See `docs/HDI_MENU_IMPLEMENTATION_PLAN.md` for the remaining phases.
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

This remains experimental software. ACA parsing is now based on observed media and strongly validated by offsets and CRCs. Standard Content command semantics and menus, and the HDi host objects scripts call, are future work.
