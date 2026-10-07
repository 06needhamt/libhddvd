# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

libhddvd is an experimental C99 library (plus small CLI tools) for inspecting HD DVD
discs and Advanced Content (HDi) menus. The long-term goal is a VLC-drivable menu engine,
but libhddvd itself must stay independent of VLC, graphics APIs, codecs, and any particular
JavaScript engine. The only external dependency is libxml2.

## Commands

```sh
# Configure, build, run all tests
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Run a single test (CTest names are the bare suffixes: core, aca, xpl, xmu, player, ...)
ctest --test-dir build -R '^xpl$' --output-on-failure
./build/test-xpl            # or run the test binary directly

# Format check exactly as CI does (CI pins clang-format 20.1.8: pip install clang-format==20.1.8)
find include src tests tools -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) \
  -print0 | xargs -0 clang-format --dry-run --Werror
# Apply formatting: replace `--dry-run --Werror` with `-i`
```

Useful CMake options: `HDDVD_BUILD_SHARED` / `HDDVD_BUILD_STATIC` (both ON by default; at
least one is required), `HDDVD_BUILD_FUZZERS` (Clang only; builds `fuzz-{aca,object,xmf,xmu,script}`),
`HDDVD_ENABLE_REFERENCE_TESTS` and `HDDVD_ENABLE_EXTERNAL_MENU_TESTS` (opt-in Python tests that
read external data from `HDDVD_REFERENCE_ACA` / `HDDVD_EXTERNAL_MENU_CORPUS` and exit 77 =
skipped when unset).

CI (`.github/workflows/ci.yml`) runs the format check, a Linux/macOS/Windows build+test
matrix, and an ASan+UBSan build with Clang. The `install-consumer` CTest installs the library
to a temp prefix and builds `tests/consumer/` (C, C++17, and static variants) against the
installed CMake package, so it catches header/export/packaging regressions; it is slow (up to
600s timeout).

## Code conventions

- Allman braces, 4-space indent, 100-column limit (`.clang-format`). CI fails on any diff.
- Library code compiles with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` (`/W4` on MSVC);
  `-Wconversion` means size/offset narrowing must be explicit and checked.
- Every public function is declared `HDDVD_API` in the single public header
  `include/hddvd.h`; symbols are hidden by default. Shared-library builds define
  `HDDVD_BUILD_SHARED` (dllexport) and consumers get `HDDVD_USE_SHARED` (dllimport).
- Tests are plain `main()` programs using `assert`; CMake compiles them with `-UNDEBUG`
  so asserts (which also perform setup) run in Release builds. Tests needing files from
  `tests/fixtures` get the path via the `HDDVD_FIXTURE_DIR` compile definition, which is
  set per target in `CMakeLists.txt`.

## Adding things

- **New source file:** add it to `HDDVD_SOURCES` in `CMakeLists.txt`.
- **New test:** create `tests/test_<name>.c` and add `<name>` to the `foreach(test ...)` list
  in `CMakeLists.txt`; add a `HDDVD_FIXTURE_DIR` definition if it reads fixtures.
- **New tool:** add to the `foreach(tool ...)` list and to the `install(TARGETS ...)` list.
- **Version bump:** keep `project(... VERSION)` in `CMakeLists.txt`, the
  `HDDVD_VERSION_*` macros in `include/hddvd.h`, `hddvd_version()` in `src/hddvd.c`,
  and the version-string asserts in `tests/test_core.c` and `tests/consumer/consumer.c`
  in sync (plus the README heading/`find_package` example).

## Architecture

Layered, bottom-up (see `docs/HDI_MENU_IMPLEMENTATION_PLAN.md` for the full design):

1. **Context & errors** (`hddvd.c`, `limits.c`, `internal.h`): `hddvd_ctx` holds the disc
   root, log callback, last-error detail, and a `hddvd_config_limits`. Context-bound
   operations report failures through `hddvd_ctx_set_error` (status, subsystem, URI,
   offset, message) and a later success clears it. All functions return `hddvd_status`
   codes (`HDDVD_OK` = 0, negatives for errors); `hddvd_strerror` must cover every code.
2. **Streams** (`io.c`, `stream.c`): opaque, refcounted `hddvd_stream` with 64-bit
   `read_at`. File, memory, and slice streams; ACA member streams retain their backing
   archive stream and outlive the `hddvd_aca` handle.
3. **Disc/resource resolution** (`disc.c`, `uri.c`, `resource.c`): directory discovery,
   canonical `file:///dvddisc/...` URIs, and case-insensitive lookup that rejects
   traversal, encoded separators, ambiguous matches, and (POSIX) symlinks.
4. **ACA archives** (`aca.c`): native `HDDVDACA` reader with strict directory/CRC
   validation; path-based functions are compatibility wrappers over the handle API.
5. **Object envelopes** (`object.c`): validates and strips the versioned `HDDVDOBJ` object
   envelope (modelled on the `AACS` envelope observed around real archive members),
   returning typed payloads. It never attempts decryption;
   protected content goes through an explicit host `hddvd_decrypt_fn` provider or fails
   with `HDDVD_EPROTECTED`. Format notes: `docs/formats/`.
6. **Immutable XML models** (libxml2): `xpl.c` (playlists/titles/clips/apps), `xmf.c`
   (manifests), `application.c` (manifest + resolved dependencies), `xmu.c` (markup tree),
   `style.c` (specified/computed styles, image-for-state, z-order, navigation).
7. **Script decoder** (`script_decode.c`): bounded structural decoder/disassembler for
   clean-room fixtures only. Commercial HDi bytecode semantics and any script runtime
   are explicitly unsupported/blocked (`HDDVD_FEATURE_SCRIPT_RUNTIME` stays unset).
8. **Display list** (`display.c`): renderer-neutral, damage-tracked quads from a scene
   tree; images are decoded by a host callback into premultiplied RGBA (no built-in
   image decoder). Includes letterbox transforms and hit testing.
9. **Navigation scheduler** (`player.c`): `hddvd_nav` event queue, timers on
   `hddvd_time`, and host command callbacks.
10. **Probes** (`evo.c`, `ifo.c`, `map.c`): EVO program-stream probing; IFO/MAP are only
    conservative probes.

Capability negotiation: hosts call `hddvd_abi_version()` and `hddvd_get_features()`.
Only set a feature bit when the capability is actually implemented.

### Limits and ABI rules

- Every parser is bounded. The unified `hddvd_config_limits` (`limits.c`) is the canonical
  set; older per-subsystem structs (`hddvd_limits`, `hddvd_xmf_limits`, ...) and their
  `*_with_limits` entry points remain for ABI compatibility. New constructors follow the
  `foo_open` / `foo_open_with_limits` / `foo_open_with_config` pattern. Raise a limit by
  making it configurable, not by bumping a hard-coded constant.
- Preserve ABI within a released major version: add functions rather than changing
  signatures or public struct layouts. New public types should be opaque unless they are
  immutable value records.

## Project rules (from `docs/IMPLEMENTATION_TASKS.md`)

- Work is tracked as task IDs (e.g. P3-01) with status, dependencies, and acceptance
  criteria in `docs/IMPLEMENTATION_TASKS.md`; update the task's status when completing it.
  Phase 4 (script runtime), P5-04, and P6-03 are currently blocked.
- A task is complete only when API docs, positive and negative tests, fuzz coverage where
  applicable, and `hddvd_strerror` behavior land in the same change.
- **Never commit, copy, or download the reference ACA or any external menu corpus** into
  the source or build tree. Fixtures under `tests/fixtures` must be original synthetic
  data (e.g. `tests/fixtures/generate_objects.py` regenerates the `.hdo` envelope
  fixtures); `test_aca.c` builds its archive at runtime.
- Production code must not special-case reference-menu filenames, element IDs,
  coordinates, or expected actions.
