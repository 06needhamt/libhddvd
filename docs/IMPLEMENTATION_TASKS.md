# HD DVD menu implementation tasks

This backlog turns Phases 1–7 of the implementation plan into reviewable work
items.  Each task is intended to fit in one pull request unless its notes say
otherwise.  Task IDs are stable so branches and pull requests can use names such
as `hdi/p1-01-stream-api`.

## How to use this backlog

* Start with a task marked **Ready** whose dependencies are complete.
* Do not use the external reference ACA as a checked-in fixture.  Synthetic test
  data belongs under `tests/fixtures`; the reference archive is supplied through
  `HDDVD_REFERENCE_ACA` only.
* A task is complete only when its API documentation, positive and negative
  tests, fuzz coverage where applicable, and `hddvd_strerror` behavior are
  included in the same change.
* Preserve ABI compatibility within a released major version.  New public types
  should be opaque unless they are immutable value records.
* Production code must not special-case reference-menu filenames, element IDs,
  coordinates, or expected actions.

## Recommended first work

The following tasks can begin immediately and can mostly proceed in parallel:

1. **P1-01** — checked 64-bit byte-stream API.
2. **P1-02** — persistent ACA archive handle, after agreeing the stream API shape.
3. **P1-04** — canonical HD DVD URI parser and resolver.
4. **P2-01** — characterize the AACS object envelope.
5. **P3-01** — immutable XMF application model using already-unwrapped synthetic
   XML.
6. **P6-01** — typed timebase and player-event vocabulary.

Before starting, open an issue or draft PR containing the proposed public API for
review.  This avoids implementing several parsers against an unstable ownership
model.

## Phase 1 — safe streams, ACA handles, and URI resolution

### P1-01 — Add the checked byte-stream API

**Status:** Complete
**Depends on:** Phase 0  
**Likely files:** `include/hddvd.h`, `src/io.c`, `src/internal.h`, `tests/test_io.c`

Add opaque `hddvd_stream` objects with `read_at`, `size`, and `close` operations.
Provide filesystem and memory implementations.  All offsets and sizes use
`uint64_t`; conversions to `size_t`, `long`, or platform APIs must be checked.
Document ownership, partial-read behavior, EOF behavior, thread affinity, and
error mapping.

**Acceptance:** memory and file streams return identical results; tests cover
zero-length reads, EOF, offsets above the end, arithmetic overflow, a file larger
than 2 GiB where the platform permits sparse files, and injected short reads.

### P1-02 — Introduce a persistent ACA archive handle

**Status:** Complete
**Depends on:** P1-01  
**Likely files:** `include/hddvd.h`, `src/aca.c`, `tests/test_aca.c`

Add `hddvd_aca_open`, lookup/iteration, member-stream, verify, and close APIs.
Parse the directory once, retain the backing stream, and stop reopening the file
for every member.  Keep the existing path-based functions as compatibility
wrappers.

**Acceptance:** the old and new APIs return the same inventory; a test host can
prove that directory bytes are parsed once; concurrent archives do not share
state; ASan/LSan report no leaks over repeated open/close cycles.

### P1-03 — Harden ACA directory validation

**Status:** Complete
**Depends on:** P1-02  
**Likely files:** `src/aca.c`, `tests/test_aca.c`, `tests/fuzz_aca.c`

Replace fixed-name truncation with owned names and validate every header field,
integer sum, directory/data boundary, entry overlap, duplicate canonical name,
padding rule, ordering rule, member limit, and file-size limit.  Add a configurable
limits structure rather than increasing hard-coded limits.

**Acceptance:** table-driven malformed fixtures exercise each rejection; errors
are deterministic; the ACA fuzzer accepts an in-memory stream and no longer
writes every input to `/tmp`.

### P1-04 — Implement canonical HD DVD URI handling

**Status:** Complete
**Depends on:** none  
**Likely files:** `src/uri.c`, `src/internal.h`, `tests/test_uri.c`

Parse `file:///dvddisc/...` URIs, percent-decode once, normalize separators and
dot segments, apply the format's case-insensitive lookup rules, and distinguish a
disc file from an ACA member.  Return a structured URI rather than a host path.

**Acceptance:** valid manifest URIs resolve identically on all hosts; tests reject
encoded separators, traversal above the disc root, NULs, invalid escapes,
ambiguous case-folded names, absolute host paths, and archive-member traversal.

### P1-05 — Add the unified resource resolver

**Status:** Complete
**Depends on:** P1-02, P1-04  
**Likely files:** `include/hddvd.h`, `src/resource.c`, `tests/test_resource.c`

Resolve structured disc URIs to a bounded `hddvd_stream`, including direct disc
files and members inside an ACA without extraction.  Add host callbacks for
alternate storage and decryption providers.  Enforce resource byte/count limits.

**Acceptance:** synthetic directory and archive layouts resolve through the same
API, requests remain confined to the configured disc root, and callbacks have
documented ownership and reentrancy behavior.

## Phase 2 — AACS object envelope

### P2-01 — Write the envelope format note and fixture generator

**Status:** Complete
**Depends on:** Phase 0  
**Likely files:** `docs/formats/aacs-object-envelope.md`,
`tests/fixtures/generate_objects.py`

Record observed header versions, object types, length fields, name area, padding,
and trailers.  Separate observations from confirmed semantics and cite the source
of each claim.  Generate original manifest, markup, PNG-signature, script, and
malformed envelopes without copying reference bytes.

**Acceptance:** the note explains every byte produced by the generator; generated
fixtures are deterministic; unknown fields remain named `unknown_N` rather than
being assigned guessed meanings.

### P2-02 — Add a strict object-envelope parser

**Status:** Complete
**Depends on:** P1-01, P2-01  
**Likely files:** `include/hddvd.h`, `src/object.c`, `tests/test_object.c`

Expose object version/type, declared length, optional name, trailer metadata, and
a bounded payload stream/view.  Validate framing before inspecting payload magic.
Accept already-unwrapped content through an explicit API path.

**Acceptance:** every generated valid type parses, every one-field corruption is
rejected, unknown types remain inspectable, and the external conformance test
classifies all 40 members without scanning for `<?xml` or PNG signatures.

### P2-03 — Model protected content explicitly

**Status:** Complete
**Depends on:** P1-05, P2-02  
**Likely files:** `include/hddvd.h`, `src/resource.c`, `src/hddvd.c`

Add `HDDVD_EPROTECTED`, a decryption-provider callback contract, and diagnostics
that distinguish malformed, unsupported, and protected objects.  Do not implement
keys or AACS circumvention.

**Acceptance:** clear objects work without a provider; protected synthetic
objects request bytes through the provider; denial returns `HDDVD_EPROTECTED`;
provider callbacks cannot recursively corrupt resolver state.

### P2-04 — Route inspection tools through object parsing

**Status:** Complete
**Depends on:** P2-02  
**Likely files:** `src/xmf.c`, `tools/hddvd-menuinfo.c`,
`tools/hddvd-acainfo.c`

Make XMF/XMU inspection accept wrapped and unwrapped inputs, and extend
`hddvd-menuinfo` JSON/text output with object type, payload range, dependencies,
and precise unsupported reasons.

**Acceptance:** `hddvd-menuinfo` gets past all reference XML envelopes and emits
valid JSON; malformed envelopes fail before libxml2 is called.

## Phase 3 — typed XMF and XMU models

### P3-01 — Implement the immutable XMF model

**Status:** Complete
**Depends on:** none for unwrapped input; integrate P2-02 later  
**Likely files:** `include/hddvd.h`, `src/xmf.c`, `tests/test_xmf.c`

Parse application id, region, ordered script references, markup references, and
resource roots into an opaque model with indexed getters.  Validate namespace,
required attributes, integer ranges, duplicate ids, and configurable node limits.

**Acceptance:** getters reproduce the synthetic manifest exactly; malformed and
foreign-namespace tests fail predictably; no libxml2 pointers escape the ABI.

### P3-02 — Implement the XMU DOM and id index

**Status:** Blocked  
**Depends on:** P3-01 ownership conventions  
**Likely files:** `include/hddvd.h`, `src/xmu.c`, `tests/test_xmu.c`

Parse supported elements into immutable nodes, retain namespaced attributes, and
build an id index in a second pass.  Preserve unknown optional elements and reject
duplicate ids, excessive depth, and excessive node/attribute counts.

**Acceptance:** the synthetic markup tree and initial focused state are queryable;
forward id references work; depth/count limits are tested and fuzzed.

### P3-03 — Parse and compute styles

**Status:** Blocked  
**Depends on:** P3-02  
**Likely files:** `src/style.c`, `tests/test_style.c`

Parse style references and the properties required by the menu: absolute pixel
geometry, `display`, `zIndex`, background-image lists, scale-to-fit, and four
directional navigation ids.  Keep specified and computed values distinct.

**Acceptance:** table tests cover inheritance/cascade order, invalid units,
integer overflow, missing references, cycles, stable z-order, and normal/focused
image selection inputs.

### P3-04 — Add dependency and capability reporting

**Status:** Blocked  
**Depends on:** P1-05, P3-01, P3-03  
**Likely files:** `include/hddvd.h`, `src/application.c`,
`tools/hddvd-menuinfo.c`

Load a complete application, resolve its references, and report all required and
unsupported features before execution.  Continue collecting diagnostics instead
of stopping at the first optional unknown.

**Acceptance:** the reference report lists every script, markup document, image,
style property, element kind, and unresolved dependency with no hard-coded names.

## Phase 4 — HDi binary scripts and runtime

### P4-01 — Inventory and document the binary script format

**Status:** Blocked on specification/research access  
**Depends on:** P2-02  
**Likely files:** `docs/formats/hdi-script.md`, `tools/hddvd-scriptinfo.c`

Create a read-only header/table inspector.  Document facts from public
specifications or independently generated programs, marking hypotheses clearly.
Do not merge guessed opcode behavior into the runtime.

**Acceptance:** the tool bounds-checks all tables and accounts for every byte in
the four reference scripts as a known structure or explicitly unknown range.

### P4-02 — Build a script decoder and disassembler

**Status:** Blocked  
**Depends on:** P4-01  
**Likely files:** `src/script_decode.c`, `tools/hddvd-scriptdump.c`,
`tests/test_script_decode.c`

Decode constants, strings, functions, instructions, branches, and exception
metadata into an engine-neutral intermediate representation.  Validate branch and
table targets before exposing the IR.

**Acceptance:** round-independent golden tests use original generated scripts;
unknown opcodes produce a structured diagnostic; fuzzing cannot create invalid IR
references or unbounded allocations.

### P4-03 — Select and isolate an ECMAScript execution backend

**Status:** Blocked  
**Depends on:** P4-02  
**Likely files:** `cmake/`, `src/script_engine.h`, `docs/decisions/`

Prototype translation to a maintained embeddable engine and compare required HDi
semantics.  Record the decision, license/packaging impact, determinism, sandbox
controls, and fallback plan.  Keep the dependency optional at build time.

**Acceptance:** a synthetic function executes with instruction, heap, recursion,
and wall-clock budgets; the library builds and reports the missing capability when
the backend is disabled.

### P4-04 — Implement DOM, focus, and event host objects

**Status:** Blocked  
**Depends on:** P3-03, P4-03  
**Likely files:** `src/runtime.c`, `src/runtime_dom.c`, `tests/test_runtime.c`

Implement element lookup, state/style mutation, listener registration, ordered
event dispatch, focus changes, navigation keys, activation, lifecycle events, and
timers.  Queue host callbacks until runtime locks are released.

**Acceptance:** deterministic replay tests cover listener ordering, focus cycles,
removed nodes/listeners, timer cancellation, exceptions, and budget exhaustion.

### P4-05 — Implement player and application host objects

**Status:** Blocked  
**Depends on:** P4-04, P6-01  
**Likely files:** `src/runtime_player.c`, `tests/test_runtime_player.c`

Expose title-clock reads, play/seek/title/chapter selection, menu visibility, and
application lifecycle through a mockable host-operations table.  Deny network,
filesystem, and process access by default.

**Acceptance:** the runtime emits typed commands and consumes acknowledgements;
unsupported calls are visible errors rather than no-ops; no callback occurs while
an internal lock is held.

### P4-06 — Reach reference-script conformance

**Status:** Blocked  
**Depends on:** P4-02 through P4-05, conforming-player trace  
**Likely files:** decoder/runtime tests and conformance report only

Close remaining opcode and host-object gaps found by the reference scripts.  Add
format-driven tests for each gap before changing runtime behavior.

**Acceptance:** the report contains zero unknown instructions and host calls, and
replay produces every captured state transition without title-specific branches.

## Phase 5 — renderer-neutral display lists

### P5-01 — Define image-decoder and display-list APIs

**Status:** Blocked  
**Depends on:** P1-05, P3-03  
**Likely files:** `include/hddvd.h`, `src/display.c`, `tests/test_display.c`

Define a host image decoder and immutable display list containing premultiplied
RGBA planes, source/destination rectangles, opacity, clipping, z-order, damage,
and hit regions.  Add byte, image-count, dimension, and decoded-pixel limits.

**Acceptance:** public ABI exposes no VLC or image-library types; malformed image
metadata cannot overflow allocations; cache ownership and eviction are tested.

### P5-02 — Compute scene layout and interaction images

**Status:** Blocked  
**Depends on:** P5-01, P4-04  
**Likely files:** `src/layout.c`, `tests/test_layout.c`

Compute nested absolute layout in the 1920x1080 logical region, clipping,
scale-to-fit, visibility, stable stacking, and normal/focused image choice.

**Acceptance:** structural snapshots for synthetic scenes cover nested offsets,
overflow, hidden ancestors, equal z-index order, focus changes, and invalid image
dimensions.

### P5-03 — Add damage tracking, transforms, and hit testing

**Status:** Blocked  
**Depends on:** P5-02  
**Likely files:** `src/display.c`, `tests/test_display.c`

Emit only changed regions, map logical coordinates through letterbox/pillarbox
transforms, and resolve mouse hits in reverse paint order.

**Acceptance:** transform tests cover common video aspect ratios and edge pixels;
hidden/clipped elements cannot be hit; focus-only changes damage the old and new
button bounds.

### P5-04 — Validate reference visual states

**Status:** Blocked  
**Depends on:** P4-06, P5-03, conforming-player screenshots  
**Likely files:** external conformance report; no menu assets

Compare display-list structure and, where color pipelines permit, screenshot
hashes for every recorded state.

**Acceptance:** geometry, visibility, resource selection, and stacking match all
captured states; any allowed pixel tolerance is documented and narrowly bounded.

## Phase 6 — playback navigation and synchronization

### P6-01 — Define timebase, event, and host-command types

**Status:** Ready  
**Depends on:** none  
**Likely files:** `include/hddvd.h`, `tests/test_events.c`

Define explicit 90 kHz title time (or one documented alternative), playback
state, discontinuity, end-of-title, stream-selection, and command acknowledgement
types.  Specify queue ordering, overflow policy, and thread affinity.

**Acceptance:** conversion boundaries and overflow are tested; no API exposes an
ambiguous integer time; mock-host tests cover success, rejection, and delayed
acknowledgement.

### P6-02 — Replace start-reference-only XPL parsing

**Status:** Blocked on fixture/spec inventory  
**Depends on:** P1-01  
**Likely files:** `include/hddvd.h`, `src/xpl.c`, `tests/test_xpl.c`

Add typed playlists, clips, title/chapter ranges, entry points, application
lifecycle declarations, and branches required by the reference title.  Preserve
the existing helper as a compatibility wrapper.

**Acceptance:** original synthetic playlists cover forward references, time
ranges, branches, malformed cycles, and limits; required reference constructs are
all represented in the conformance report.

### P6-03 — Decode required IFO and MAP navigation structures

**Status:** Blocked on fixture/spec inventory  
**Depends on:** P1-01, P6-02  
**Likely files:** `src/ifo.c`, `src/map.c`, `tests/test_ifo.c`, `tests/test_map.c`

Replace size/signature probes with typed, bounds-checked structures required to
map titles and time positions to EVO ranges.  Work in small PRs per documented
structure if necessary.

**Acceptance:** synthetic binary builders exercise every field and endian
conversion; malformed offsets/counts are rejected; no parser casts input bytes to
C structs.

### P6-04 — Harden EVO packet and stream metadata parsing

**Status:** Ready after stream API  
**Depends on:** P1-01  
**Likely files:** `src/evo.c`, `tests/test_evo.c`

Parse packets incrementally across arbitrary read boundaries, validate PES
length/header fields, and expose stream/angle/audio/subtitle metadata needed by
VLC without decoding elementary streams.

**Acceptance:** every split position for representative headers is tested;
truncation and oversized lengths fail safely; packet counters do not double-count
overlap bytes.

### P6-05 — Add the deterministic navigation scheduler

**Status:** Blocked  
**Depends on:** P4-05, P6-01, P6-02  
**Likely files:** `src/player.c`, `tests/test_player.c`

Serialize clocks, input, script tasks, timers, playback acknowledgements, seeks,
pauses, rate changes, discontinuities, and title-end events on one navigation
queue.

**Acceptance:** fake-clock tests prove stable ordering and timer behavior across
pause, seek, rate change, discontinuity, title end, menu close, and reopen.

## Phase 7 — VLC integration

These tasks belong in the VLC source tree unless a small adapter can be tested in
this repository.  Pin the libhddvd API/ABI version in every integration PR.

### P7-01 — Publish consumable build metadata and capabilities

**Status:** Blocked  
**Depends on:** stable APIs from P1, P3, P4, P5, and P6  
**Likely files:** `CMakeLists.txt`, `libhddvd.pc.in`, installed CMake config

Install package metadata, ABI version information, and queryable feature bits for
object envelopes, markup, scripts, rendering, navigation, and protection.

**Acceptance:** clean C and C++ consumer projects build against both installed
shared and static libraries and can report exactly which optional backends exist.

### P7-02 — Implement the VLC access/demux module

**Status:** Blocked  
**Depends on:** P1-05, P6-02 through P6-04, P7-01

Open an HD DVD root, provide authorized clear streams, map title/EVO ranges to
VLC demuxing, publish chapters and streams, and forward playback state to the
navigation scheduler.

**Acceptance:** headless tests open synthetic clear layouts; seek/title/chapter
commands land on the expected ranges; protected content produces an actionable
message rather than a generic I/O error.

### P7-03 — Implement VLC overlay and input bridging

**Status:** Blocked  
**Depends on:** P5-03, P6-05, P7-02

Convert display-list changes to VLC subpictures and translate VLC key/mouse/menu
actions to HDi events on a serialized worker.  Queue acknowledgements to avoid
recursive entry into libhddvd.

**Acceptance:** a mock-host integration test covers key, mouse, damage, resize,
letterbox, close, and reopen paths; ThreadSanitizer finds no event/clock races.

### P7-04 — Run end-to-end reference conformance

**Status:** Blocked  
**Depends on:** P4-06, P5-04, P6-05, P7-03

Run the complete interaction trace against VLC with clear and, where an authorized
provider is available, protected media.  Exercise repeated open/close and long
playback under leak and race detectors.

**Acceptance:** all captured interactions and playback commands pass, audio/video
remain synchronized behind the overlay, zero required capabilities are unknown,
and failures identify the missing feature or protection provider.

## Cross-cutting tasks

### X-01 — Error detail API

**Status:** Ready  
**Depends on:** none

Add structured per-context diagnostics containing status, subsystem, byte offset,
resource URI, and a human-readable message while retaining `hddvd_strerror` for
stable status names.

### X-02 — Configurable limits

**Status:** Ready after P1-01 API review  
**Depends on:** P1-01 ownership conventions

Define one limits object for archive members, resource bytes, XML nodes/depth,
decoded pixels, script heap/instructions/recursion, timers, and queued events.
Provide conservative defaults and tests proving every limit is enforced.

### X-03 — CI hardening matrix

**Status:** Ready  
**Depends on:** none

Add GCC and Clang builds, ASan/UBSan, a Clang fuzzer compile check, install/consumer
tests, and Windows/macOS jobs where project infrastructure permits.  The external
reference archive remains absent from public CI.

## Milestone completion order

1. **Resource milestone:** P1-01 through P1-05, P2-01 through P2-04.
2. **Static menu milestone:** P3-01 through P3-04, P5-01, and P5-02; this can
   inspect and render authored initial state but cannot execute interactions.
3. **Interactive menu milestone:** P4-01 through P4-06, P5-03, and P5-04.
4. **Playback milestone:** P6-01 through P6-05.
5. **VLC milestone:** P7-01 through P7-04.

Do not call the static milestone “menu support”: reference-menu support requires
the interactive, playback, and VLC milestones as well as the Phase 0 conformance
trace.
