# libhddvd 0.1.0

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
- Conservative IFO/MAP probes (binary semantic decoding is still incomplete)
- A bounded, engine-neutral script-table decoder and disassembler for clean-room
  structural fixtures; commercial HDi bytecode remains explicitly unsupported
- Renderer-neutral, damage-tracked display lists with host-decoded premultiplied
  RGBA planes, nested clipping/layout, stable stacking, focus image selection,
  letterbox transforms, and reverse-paint-order hit testing

## ACA tools

```sh
hddvd-acainfo archived.aca
hddvd-acaextract archived.aca extracted
hddvd-menuinfo archived.aca
```

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

Parser fuzz harnesses can be built with Clang and
`-DHDDVD_BUILD_FUZZERS=ON`; the checked-in inputs under `tests/fixtures` are
original synthetic seeds and contain no bytes from the reference archive.

## Status

This remains experimental software. ACA parsing is now based on observed media and strongly validated by offsets and CRCs. Full IFO/MAP semantics and an HDi JavaScript/rendering runtime are future work.
