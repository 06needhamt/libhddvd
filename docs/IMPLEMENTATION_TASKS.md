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

The note now also records the on-disc `AACS` layout observed across 1,647
objects from five discs. `hddvd_object_open` parses it and the XPL, XMF and XMU
parsers unwrap it, so real playlists parse; protected types report
`HDDVD_EPROTECTED`.

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

**Status:** Complete
**Depends on:** P3-01 ownership conventions
**Likely files:** `include/hddvd.h`, `src/xmu.c`, `tests/test_xmu.c`

Parse supported elements into immutable nodes, retain namespaced attributes, and
build an id index in a second pass.  Preserve unknown optional elements and reject
duplicate ids, excessive depth, and excessive node/attribute counts.

**Acceptance:** the synthetic markup tree and initial focused state are queryable;
forward id references work; depth/count limits are tested and fuzzed.

Real discs showed three relaxations, each now covered by synthetic tests:
manifests usually omit the application id and give script and markup
references only a `src` (ids, when present, must be non-empty and unique);
`state:focused` on timing templates under `<head>` is not the initial focus;
and markup fragments use a `<div>` root. All 91 manifest and markup files from
five externally supplied discs now parse.

### P3-03 — Parse and compute styles

**Status:** Complete
**Depends on:** P3-02
**Likely files:** `src/style.c`, `tests/test_style.c`

Parse style references and the properties required by the menu: absolute pixel
geometry, `display`, `zIndex`, background-image lists, scale-to-fit, and four
directional navigation ids.  Keep specified and computed values distinct.

**Acceptance:** table tests cover inheritance/cascade order, invalid units,
integer overflow, missing references, cycles, stable z-order, and normal/focused
image selection inputs.

Real discs added `display: auto`, whole-percentage `width`/`height` (reported
through `HDDVD_XMU_STYLE_WIDTH_PERCENT`/`HEIGHT_PERCENT`), `position: relative`,
navigation targets of `none` or ids a script may create later (both resolve to
no target), keyframe lists on `<timing>` animations (which carry no static
style), and fragments that reference styles defined by the including document.
Styles now compute for all 39 markup files from five externally supplied discs.

The same 39 files use about 40 style properties. `hddvd_xmu_get_style_details` now models every property they use, apart from keyword font sizes:

| Group | Properties |
|---|---|
| Rendering | `opacity`, `visibility`, `color`, `backgroundColor`, `backgroundRepeat`, `backgroundPositionHorizontal`/`Vertical`, `backgroundFrame`, `crop` |
| Text | `font`, `fontSize`, `fontStyle`, `textAlign`, `displayAlign`, `wrapOption`, `textAltitude`, `anchor`, `border` |
| Padding | `padding`, `paddingBefore`/`After`/`Start`/`End` |
| Content size | percentage `contentWidth` and `contentHeight` |
| Navigation | `navLeftUp`, `navLeftDown`, `navRightUp`, `navRightDown`, `navIndex` |

Values and parsing rules:
- **Colors** are 0xRRGGBBAA. They are parsed from `#rrggbb`, `rgb()`, `rgba()` and the 16 basic colour names.
- **Opacity** is stored in thousandths.
- **`inherit`** is recorded as declared, because computed values do not inherit from parent elements.
- **Kept as written:** `anchor`, `border` and `crop`, because their meaning is unconfirmed.
- **Malformed numbers** fail the document, as they do for the older properties.
- **Unrecognised keywords and unknown properties** are reported through `hddvd_xmu_styles_get_unmodeled` instead, so a new value never makes a whole menu unusable.

On the real files, the only things still reported are keyword font sizes (`small`, `xx-large`). How they map to pixels is not established.

### P3-04 — Add dependency and capability reporting

**Status:** Complete
**Depends on:** P1-05, P3-01, P3-03
**Likely files:** `include/hddvd.h`, `src/application.c`,
`tools/hddvd-menuinfo.c`

Load a complete application, resolve its references, and report all required and
unsupported features before execution.  Continue collecting diagnostics instead
of stopping at the first optional unknown.

**Acceptance:** the reference report lists every script, markup document, image,
style property, element kind, and unresolved dependency with no hard-coded names.

## Phase 4 — HDi scripts and runtime

HDi scripts on disc are ECMAScript 3 source text (UTF-16BE), bare or inside the
clear `AACS` envelope; protected ones are encrypted source, not a binary format
(`docs/formats/hdi-script.md`). The work is therefore choosing an ECMAScript
engine and implementing the HDi host objects, not decoding bytecode.

### P4-01 — Inventory the script format and host API

**Status:** Complete
**Depends on:** P2-02
**Likely files:** `docs/formats/hdi-script.md`, `docs/hdi-host-api.md`,
`tools/inventory_hdi_scripts.py`, `tools/hddvd-scriptinfo.c`

`hddvd-scriptinfo` bounds-checks envelopes and clean-room script tables and
labels every byte as `structure`, `observed`, or `unknown`; CTest pins the
accounting. The five protected (type `0x11`) scripts on the ripped discs account
as 11 bytes of envelope fields and an encrypted payload.

The 126 readable scripts on the ripped discs are ECMAScript source.
`tools/inventory_hdi_scripts.py` lists the host globals, member chains, events
and methods they use, without copying source; `docs/hdi-host-api.md` records
the result. The `hdi-api-inventory` CTest pins the tool's report for original
fixture scripts.

**Acceptance:** every byte of a script object is accounted for, and the host API
used by every readable script on the available discs is listed.

### P4-02 — Build a script decoder and disassembler

**Status:** Not needed for disc content; the clean-room `HDIS` decoder stays as a fixture-only tool
**Depends on:** P4-01
**Likely files:** `src/script_decode.c`, `tools/hddvd-scriptdump.c`,
`tests/test_script_decode.c`

This task assumed discs carry compiled scripts. They carry source, which an
ECMAScript engine parses directly, so there is no disc format to decode.
`hddvd_script_open` and `hddvd-scriptdump` remain as a bounded structural
decoder for the clean-room `HDIS` fixtures and keep their tests and fuzzing.

### P4-03 — Select and isolate an ECMAScript execution backend

**Status:** Complete
**Depends on:** P4-01
**Likely files:** `cmake/`, `src/script_engine.h`, `docs/decisions/`

Choose a maintained embeddable engine that runs ECMAScript 3 source decoded from
UTF-16BE, such as QuickJS or Duktape. Record the decision, license/packaging
impact, determinism, sandbox controls, and fallback plan. Keep the dependency
optional at build time.

`docs/decisions/0001-script-engine.md` records the choice of QuickJS. The public
`hddvd_runtime` API evaluates UTF-16 or UTF-8 source and calls global
functions, with per-runtime heap and per-entry recursion, operation and
wall-clock budgets (`hddvd_runtime_limits`, or the config's script fields).
Results distinguish syntax errors (`HDDVD_EPARSE`), uncaught exceptions
(`HDDVD_ESCRIPT`) and exhausted budgets (`HDDVD_ELIMIT` with the budget named).
The engine sits behind `src/script_engine.h`, is selected by
`HDDVD_SCRIPT_ENGINE`, and is either compiled from a QuickJS release
(`HDDVD_QUICKJS_SOURCE_DIR`) or linked from an installed library. Without one,
the runtime reports `HDDVD_ENOTSUP` and no `HDDVD_FEATURE_SCRIPT_RUNTIME`.
`test_runtime.c`, `test_limits.c`, `test_threads.c` and `fuzz-runtime` cover
both builds.

**Acceptance:** a synthetic function executes with instruction, heap, recursion,
and wall-clock budgets; the library builds and reports the missing capability when
the backend is disabled.

### P4-04 — Implement DOM, focus, and event host objects

**Status:** Complete
**Depends on:** P3-03, P4-03
**Likely files:** `src/runtime.c`, `src/runtime_dom.c`, `tests/test_runtime.c`

Implement element lookup, state/style mutation, listener registration, ordered
event dispatch, focus changes, navigation keys, activation, lifecycle events, and
timers.  Queue host callbacks until runtime locks are released.  The members to
cover first are the `document`, element, `application`, event and timer entries
in `docs/hdi-host-api.md`.

The host objects are JavaScript (`src/hdi_prelude.js`, embedded at build time)
over engine-neutral natives in `src/runtime_dom.c`, reached through two new
engine-boundary calls (`hddvd_engine_install`, `hddvd_engine_invoke`). The host
loads markup (`hddvd_runtime_load_markup`), calls the markup loaded handler,
sends keys, time and events (`hddvd_runtime_key`, `_advance`, `_dispatch`), and
reads back the focus, a change counter, state and style values, issues
(listener exceptions and style values the model cannot apply), and the
document's computed styles with script values applied
(`hddvd_runtime_styles`), which `hddvd_scene_build` takes as they are. The
runtime makes no callbacks at all, so none can happen inside the engine.
`include/hddvd.h` documents every member and model choice; choices no player
has confirmed (key defaults, timer frame rate, animation timing) are marked
unconfirmed there and in `docs/hdi-host-api.md`.

`test_runtime_dom.c` covers lookups, state, style, listener ordering and
removal during dispatch, exceptions, focus cycles and keys, timers (ordering,
cancellation, one-shot, catch-up), animations, document replacement, budgets
and replay across two runtimes. `fuzz-runtime` now runs scripts against a
document and drives keys, time and events. `hddvd-menurun` runs a disc
application; its CTest runs a generated ACA application. On the five ripped
discs, all 52 applications load: every readable script evaluates except 16
that need `PersistentStorageManager`, `XMLParser` or `Player` at load time, and
the remaining failures are all P4-05 members (`Player`,
`application.thisAdvancedApplication`, `moveToTop()`) or markup timing
(`document.setXPathVariable()`).

**Acceptance:** deterministic replay tests cover listener ordering, focus cycles,
removed nodes/listeners, timer cancellation, exceptions, and budget exhaustion.

**Markup timing** now runs too (`src/hdi_prelude.js`, over timing layers in
`src/runtime_dom.c`). It covers what the five discs' markup uses:
- `par` and `cue` containers that begin at an offset or when a condition
  becomes true, and end after `dur`, at an end offset, on an end condition,
  or with their parent;
- `fill="hold"`;
- `set`, `animate` (linear or discrete) and `event` (with `param`s as event
  fields), directly or through `use=` of `<defs>`;
- an XPath subset for conditions and `select`: `id()`, `class()`,
  `defaultNode()`, `//name`, `@name`, `$variables` from
  `document.setXPathVariable()`, `state:` and `style:` functions,
  predicates, comparisons, `and`, `or` and `|`.

Timing values sit below script style values and held script state values;
`state.unsetProperty()` now hands a property back to timing. Unparsable
expressions and unsupported elements become `HDDVD_RUNTIME_ISSUE_TIMING`
issues. `test_runtime_timing.c` covers offsets, fill, conditions, events,
focus set by timing, variables, definitions, animation, precedence, restarts,
issues and document replacement; `fuzz-runtime` also runs each input as the
contents of a `<timing>` element. Every disc expression parses and no disc
application reports a timing issue. The model follows SMIL and is unconfirmed
against a player.

### P4-05 — Implement player and application host objects

**Status:** Complete
**Depends on:** P4-04, P6-01
**Likely files:** `src/runtime_player.c`, `tests/test_runtime_player.c`

Expose title-clock reads, play/seek/title/chapter selection, menu visibility, and
application lifecycle through a mockable host-operations table.  Deny network,
filesystem, and process access by default.  `docs/hdi-host-api.md` lists the
`Player` members discs use; `FileIO`, `XMLParser`, `Network` and
`PersistentStorageManager` start as stubs that report failure.

**Acceptance:** the runtime emits typed commands and consumes acknowledgements;
unsupported calls are visible errors rather than no-ops; no callback occurs while
an internal lock is held.

The host-operations table took the polling form P4-04 set up, so there are no
callbacks at all.
- **Host input:** the host loads the playlist (`hddvd_runtime_load_playlist`)
  and reports the player state (`hddvd_runtime_set_player`: title, title time,
  chapter, play state, tracks, subtitle visibility, menu language). It also
  reports the running applications in z-order (`hddvd_runtime_set_applications`)
  and dispatches player events with string fields
  (`hddvd_runtime_dispatch_fields`).
- **Script objects:** scripts get `Player.playlist` (titles by index and id,
  chapters, the current title and its elapsed time, play state), `Player.track`,
  `Player.subtitle`, `Player.generalParameters`, `Diagnostics.trace`, and
  `application`'s lifecycle members.
- **Commands:** calls that change playback queue typed commands
  (`hddvd_runtime_command`: play, pause, jump, track and language selection,
  subtitle visibility, application activation and stacking). They are bounded
  at 64 waiting and 64 unacknowledged, and the host takes and acknowledges
  each. Jumps are checked against the playlist, including the title's
  duration. Rejected and failed commands become issues.
- **Unsupported and denied members:** members a player has but this runtime
  cannot honor throw an Error naming the member. These are audio mixing and
  effects, video layout, `getSPRM`, `document.getProperties`, and every
  member of `FileIO`, `XMLParser`, `Network` and `PersistentStorageManager`,
  whose file and network access is denied. Their constants exist so scripts
  load.
- **Tests:** `test_runtime_player.c` covers titles, chapters and stale title
  objects, the player state, every command type, the queue limits,
  acknowledgements, applications, parameters, the stubs, traces and events with
  fields. `fuzz-runtime` loads a playlist, reports state and acknowledges
  commands.
- **`hddvd-menurun`:** given a playlist, it finds the title that runs the
  application and reports it, prints and accepts commands, and a CTest checks
  a jump.
- **Disc results:** on the five discs, the remaining failures are all of two
  kinds: denied XML and storage access, and unsupported audio mixing. The only
  exceptions are scripts that read a title or bookmark the host never
  reported, or state that a denied call left unset. (Markup timing, the third
  kind before, now runs; see P4-04.)

### P4-06 — Reach reference-script conformance

**Status:** Blocked
**Depends on:** P4-02 through P4-05, conforming-player trace
**Likely files:** decoder/runtime tests and conformance report only

Close remaining host-object gaps found by the reference scripts.  Add tests for
each gap before changing runtime behavior.

The replay side is ready (`docs/REFERENCE_TRACE.md`):
`tests/replay_reference_trace.py` checks a recorded trace against
`hddvd-menurun --state`, transition by transition: focus, visible elements and
player commands. CTest runs it on a hand-written trace of the fixture
application, and the opt-in `reference-trace` test runs it on a trace named by
`HDDVD_REFERENCE_TRACE`. What is still missing is a trace recorded on a
player.

**Acceptance:** the report contains zero unknown host calls, and replay produces
every captured state transition without title-specific branches.

## Phase 5 — renderer-neutral display lists

### P5-01 — Define image-decoder and display-list APIs

**Status:** Complete
**Depends on:** P1-05, P3-03
**Likely files:** `include/hddvd.h`, `src/display.c`, `tests/test_display.c`

Define a host image decoder and immutable display list containing premultiplied
RGBA planes, source/destination rectangles, opacity, clipping, z-order, damage,
and hit regions.  Add byte, image-count, dimension, and decoded-pixel limits.

**Acceptance:** public ABI exposes no VLC or image-library types; malformed image
metadata cannot overflow allocations; cache ownership and eviction are tested.

### P5-02 — Compute scene layout and interaction images

**Status:** Complete
**Depends on:** P5-01, P4-04
**Likely files:** `src/layout.c`, `tests/test_layout.c`

Compute nested absolute layout in the 1920x1080 logical region, clipping,
scale-to-fit, visibility, stable stacking, and normal/focused image choice.

**Acceptance:** structural snapshots for synthetic scenes cover nested offsets,
overflow, hidden ancestors, equal z-index order, focus changes, and invalid image
dimensions.

`hddvd_scene_build` (`src/scene.c`) produces the scene nodes from markup and its computed styles, so hosts no longer build them by hand.

| Scene field | Comes from |
|---|---|
| Nodes | Elements under `<body>` (or a whole `<div>` fragment), in document order, except `param` and `include` |
| Node ids | The element's markup node index, so hit tests name markup nodes |
| Geometry | `x`, `y`, `width`, `height` in pixels, or a percentage of the parent |
| `visible` | Cleared by `display: none` or `visibility: hidden` |
| Opacity | CSS rules: `inherit` copies the parent's value, and values multiply down the tree |
| Other fields | `zIndex`; focus on the requested node; buttons and inputs are hit-testable |
| Images | Normal and focused background images, fetched once per URL through a host callback |

Three rules are not confirmed against a player:
- An element without a declared size fills its parent. 101 containers in the real markup have no declared size; sizing them to zero would clip everything inside them.
- A hidden element also hides its descendants, as the display list already does. CSS would still show a descendant marked `visible`.
- `position: relative` is placed the same way as `absolute`.

Background position, colours and text are now drawn as well, without changing any existing record.

- **Scene decorations:** each scene node has a decoration record (`hddvd_scene_get_decoration`) holding:
  - its `backgroundColor`;
  - its text, from a `<p>` element's text or an `<input>`'s `state:value`, with font, size, colour, alignment and wrapping;
  - its image placement, from `backgroundPosition` and percentage `contentWidth`/`contentHeight`.
- **Image objects:** `<object type="image/...">` elements draw their `src`. 40 real objects use this; it was previously missed, because their image is not a `backgroundImage`.
- **Display items:** `hddvd_display_build_scene` turns a scene into fill, image and text items, in that order within a node (`hddvd_display_get_item`). Damage and hit tests cover every item. `hddvd_display_quad_count`/`get_quad` still list image quads only, so existing hosts see no change.
- **Image placement:** an image is drawn at its own size and positioned only when `backgroundPosition` is declared. Otherwise it is still stretched to the node.
- **Not drawn:** crop (its meaning is unconfirmed) and border.

Checked with `hddvd-menuinfo --scene` against the real archives:
- Every markup file builds a display list.
- Image objects add 3 drawn images to the initial Fast & Furious main menu, and the rating card to `mpaa.xmu`.
- The on-screen keyboard draws 45 key-label text runs. Its keys are authored at `0,0` and arranged by script at runtime.

Results on the 39 real markup files:
- All build scenes, and every image URL they name resolves to a member of the same archive.
- Most content is authored hidden: panels start as `display: none` or `visibility: hidden` until a script reveals them.
- In its initial state, the visible main page of `MainMenu.xmu` draws 6 quads.

### P5-03 — Add damage tracking, transforms, and hit testing

**Status:** Complete
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

The trace format records a screenshot hash per state, and the replay harness
(P4-06) already reaches every recorded state; it does not compare pixels,
because libhddvd draws nothing itself.

**Acceptance:** geometry, visibility, resource selection, and stacking match all
captured states; any allowed pixel tolerance is documented and narrowly bounded.

## Phase 6 — playback navigation and synchronization

### P6-01 — Define timebase, event, and host-command types

**Status:** Complete
**Depends on:** none
**Likely files:** `include/hddvd.h`, `tests/test_events.c`

Define explicit 90 kHz title time (or one documented alternative), playback
state, discontinuity, end-of-title, stream-selection, and command acknowledgement
types.  Specify queue ordering, overflow policy, and thread affinity.

**Acceptance:** conversion boundaries and overflow are tested; no API exposes an
ambiguous integer time; mock-host tests cover success, rejection, and delayed
acknowledgement.

### P6-02 — Replace start-reference-only XPL parsing

**Status:** Complete; all five externally supplied playlists pass the strict conformance check
**Depends on:** P1-01
**Likely files:** `include/hddvd.h`, `src/xpl.c`, `tests/test_xpl.c`

The immutable typed model now covers configuration, media attributes, first-play
and numbered titles, primary/substitute/secondary audio-video and advanced-subtitle
clips, chapters, track navigation, application declarations
and resources, application loading times, scheduled events, pause points,
normalized frame times, and configurable node/item limits. Title ids and numbers
are unique, title-scoped times are checked against declared durations, chapter
entry points begin at zero and increase in document order, and forward
`onEnd` branches are validated and exposed as title indices. The existing
start-reference helper remains as a compatibility wrapper. Playlist versions,
TitleSet `tickBase` and `defaultLanguage` are modeled, and
`hddvd_xpl_get_application_state` derives each application's authored
inactive/loading/active window from `loadingBegin` and its title times.
`hddvd_xpl_get_unmodeled` reports every element or attribute that parsed but is
not represented, so nothing is silently dropped. `hddvd-xplinfo` exposes this
as JSON; the external inventory records it per playlist, and setting
`HDDVD_EXTERNAL_MENU_STRICT=1` makes the external-menu-corpus test fail on any
rejected playlist or unmodeled construct. The `xpl-inventory` CTest runs the
same strict check on the synthetic fixtures, and `fuzz-xpl` covers the parser.

The five playlists from the maintainer's discs left these constructs unmodelled. Each now has an accessor:

| Construct | Accessor |
|---|---|
| `description` on the playlist | `hddvd_xpl_description` |
| `description`, `alternativeSDDisplayMode` and `tickBaseDivisor` on titles | `hddvd_xpl_get_title_details` |
| `description` on clips | `hddvd_xpl_get_clip_description` |
| `description`, `angleNumber` and `forced` on tracks | `hddvd_xpl_get_track_details` |
| `id` and `sync` on applications | `hddvd_xpl_get_application_details` |
| `description`, `priority` and `loadingBegin` on resources | `hddvd_xpl_get_application_resource_details` |

- **Raw values:** `sync` and `alternativeSDDisplayMode` are kept as authored, because only some of their values have been seen. `sync` is also given as an enum, with `OTHER` for values outside hard, soft and none.
- **`SubAudio`:** read as an audio track. Like `SubVideo`, it is marked as a secondary stream.
- **`xsi:` attributes:** these describe the XML document, not the playlist, and are no longer reported as unmodelled.

All five real playlists now report no unmodelled constructs.

**Acceptance:** original synthetic playlists cover forward references, time
ranges, branches, malformed cycles, and limits; required reference constructs are
all represented in the conformance report.

### P6-03 — Decode required IFO and MAP navigation structures

**Status:** Complete (attribute bits, angle layouts and other time bases that no available disc shows are listed below)
**Depends on:** P1-01, P6-02
**Likely files:** `src/ifo.c`, `src/map.c`, `tests/test_ifo.c`, `tests/test_map.c`

Replace size/signature probes with typed, bounds-checked structures required to
map titles and time positions to EVO ranges.  Work in small PRs per documented
structure if necessary.

**Acceptance:** synthetic binary builders exercise every field and endian
conversion; malformed offsets/counts are rejected; no parser casts input bytes to
C structs.

First slice: `hddvd_ifo_read_header` and `hddvd_map_read_header` recognize the
identifiers seen on 141 real files from five discs, require whole 2048-byte
sectors and an end-sector field that matches the size, and read the VTI name
each MAP refers to. `docs/formats/ifo-map.md` records the evidence. The supplied
files include no VTI or EVO files, so nothing yet confirms what the time-map or
IFO tables point to.

Second slice: one disc's `HVA00001.VTI` and one of its EVO files showed what the
tables mean. `hddvd_vti_open` reads the VTI's EVOB table (file name, number,
start and end time, sector count). `hddvd_tmap_open` decodes every MAP entry as
one EVOBU: first-reference size, playback time in 3003/8-tick units, and size.
Entry sizes match the EVO's navigation packs. On that disc both maps sum to
their VTI records exactly, and all 130 MAPs from five discs open.
`hddvd_tmap_find` maps a time within an EVOB to an EVOBU and its first sector.
Synthetic builders in `test_ifo.c` and `test_map.c` cover every field and
rejection, `fuzz-timemap` covers both readers, and `hddvd-timemap` checks real
files.

Third slice: `hddvd_title_map_open` connects a playlist title to its media. It
opens the time map of each primary audio/video clip and the VTI that map names,
and checks that the clip fits its map. `hddvd_title_map_locate` then resolves a
title time to the EVO URI, its presentation time, and the sector of the EVOBU
that plays it. Playlist times count `timeBase="60fps"` at 60000/1001 frames per
second (`hddvd_xpl_time_to_ticks`). On both 300 discs, all 86 clips are exactly
as long as their maps at that rate, and shorter at a true 60. On the South Park
disc, `hddvd-locate` reproduces the EVOBU start times in `FBILogo.EVO`'s
navigation packs. `test_title_map.c` covers a synthetic disc tree.

Fourth slice: Standard Content. `hddvd_vts_open` reads a title set's chapters,
PGCs (programs, cells, raw commands), time maps, cell address table and VOBU
map, rejecting any reference that does not resolve. `hddvd_vmg_open` reads the
disc's title table. The layout is DVD-Video's with HD DVD record sizes (12-byte
PGC search pointers, 28-byte cells, 16-byte titles). All 11 Dragon's Lair IFOs
open, and `hddvd-vtsinfo` verifies one title set against its complete EVO:
navigation packs, cell durations and every time-map entry. `test_vts.c` covers
an original synthetic disc and every rejection, `fuzz-vts` both readers.

Fifth slice: commands and stream attributes.
- **Command table fix:** the command table header is 10 bytes (three counts and
  a 4-byte end offset), not 8, which the fourth slice had wrong. Every command
  read through it was shifted by 2 bytes. All 101 tables on the disc agree,
  and open now checks the end offset.
- **Command encoding:** the 785 commands follow DVD-Video's encoding.
  `hddvd-vtsinfo` prints them in that syntax and checks every target: link
  cells, programs and chapters, jump titles and chapters, and goto lines.
  None is invalid. Register 63 is used, so HD DVD has at least 64 general
  registers. One form (jump subset 8, 212 commands) does not fit DVD's
  `CallSS` and is reported as unconfirmed.
- **Stream attributes:** `hddvd_vts_get_video_attributes` and
  `hddvd_vts_get_stream_attributes` return a title set's video coding and its
  audio and subpicture languages, plus every record as stored.
  `hddvd_vti_get_video_attributes` and `hddvd_vti_get_audio_attributes` do the
  same for VTI attribute records. Two discs show that the coding bits
  distinguish MPEG-2 from VC-1, and that the counts match the EVOs that have
  no audio or subpictures. `hddvd-vtsinfo` checks the coding and the counts
  against a title EVO's streams. Synthetic fixtures, mutations in `test_vts.c`
  and `test_ifo.c`, and both fuzzers cover the new fields.

Sixth slice: menus, first play and command execution.
- **Menus:** the menu PGC unit tables (title set 0xd0, VMG 0xc8) hold language
  units of command-only PGCs. `hddvd_vts_get_menu_*` and
  `hddvd_vmg_get_menu_*` read them; the category's menu id names the VMG
  title menu (2) and each set's root menu (3). The VMG's first-play PGC lies in
  its header at the byte offset in 0x84 (`hddvd_vmg_get_first_play*`). All 11
  IFOs open: 9 menu tables, 18 menu PGCs and 1 first play.
- **Domain fix:** jump subset 8 is `CallSS`. Its domain is in bits 5 and 4 of
  byte 5, as for `JumpSS`, which the fifth slice read wrong. All 212 `CallSS`
  commands and 6 `JumpSS` name VMG PGCs that exist, or a root menu its set
  has. `hddvd-vtsinfo` now confirms all 1,094 commands but one, a `SetSystem`
  sub-operation DVD does not define. It also decodes conditional sets and
  `SetSTN`, prints both menu tables and the first play, and runs the first play.
- **VM:** `hddvd_vm` runs command lists with libdvdnav's semantics: 64 general
  and 32 system registers, conditions, `Goto`/`Break`, sets, `SetSTN`,
  `SetHL_BTNN`, links and jumps. It returns the link or jump for the host to
  follow. It never runs an unknown form; it reports it, and bad registers or
  lines fail with `HDDVD_EPARSE`. `HDDVD_ELIMIT` stops a loop after 65,536
  commands.
- **Coverage:** `test_vm.c` covers every operation and rejection. `test_vts.c`
  reads the synthetic menus and first play, rejects bad pointers, and runs the
  fixture VMG's first play through its menus. `fuzz-vts` walks the menus and
  runs its input as commands, from a new seed.

Not decoded, for lack of evidence (no disc with them is available):
- audio coding and channel bits (no EVO with audio);
- menu EVOs and menu cells;
- angle and interleaved maps;
- the clock for time bases other than 60fps;
- the cell command flags and bit 7 of the `JumpSS`/`CallSS` domain byte.

### P6-04 — Harden EVO packet and stream metadata parsing

**Status:** Complete
**Depends on:** P1-01
**Likely files:** `src/evo.c`, `tests/test_evo.c`

Parse packets incrementally across arbitrary read boundaries, validate PES
length/header fields, and expose stream/angle/audio/subtitle metadata needed by
VLC without decoding elementary streams.

**Acceptance:** every split position for representative headers is tested;
truncation and oversized lengths fail safely; packet counters do not double-count
overlap bytes.

Follow-up (October 2026): extended-stream-id (`0xfd`) packets now take their
stream from the PES header extension's `stream_id_extension`, not the first
payload byte. On `FBILogo.EVO` that turns dozens of spurious streams into its
one VC-1 stream (`0x55`). That EVO is AACS-scrambled, and 23 of its packs
end with bytes no packet covers. The parser skips them, but the file's last
pack is one of them, so `finish()` still reports `HDDVD_EPARSE`. What those
bytes are cannot be told without decryption.

### P6-05 — Add the deterministic navigation scheduler

**Status:** Complete
**Depends on:** P4-05, P6-01, P6-02
**Likely files:** `src/player.c`, `tests/test_player.c`

Serialize clocks, input, script tasks, timers, playback acknowledgements, seeks,
pauses, rate changes, discontinuities, and title-end events on one navigation
queue.

Timers run on title time. `hddvd_nav_discontinuity` handles seeks in either
direction and stream discontinuities without reordering queued events,
`HDDVD_EVENT_RATE_CHANGE` reports rate changes, and
`hddvd_nav_cancel_all_timers` discards a closed menu's timers. Fake-clock tests
in `test_player.c` cover every case in the acceptance criteria.

Script tasks run on the same queue through `hddvd_runtime_bridge`
(`src/runtime_bridge.c`). The host passes every pumped event to
`hddvd_runtime_bridge_event`, which:
- moves the runtime clock by the scheduler time that passed, so script timers
  and animations follow title time and do not move across a seek;
- turns input into key events;
- reports title, chapter, play state and track changes as player state and
  dispatches `title_begin`, `title_end`, `chapter`, `play_state`,
  `audio_track` and `subtitle_track` (new event types `HDDVD_EVENT_TITLE_BEGIN`,
  `HDDVD_EVENT_CHAPTER`, and `hddvd_stream_type` for stream selections);
- hands the commands scripts made to the host in order, and takes the host's
  answers back as `HDDVD_EVENT_SCRIPT_ACK` events, so acknowledgements are
  ordered with everything else. Commands beyond the runtime's 64
  unacknowledged wait for answers.

`test_runtime_bridge.c` runs a scheduler with a runtime and checks event
order and the state listeners see, timers across a seek, the command round
trip (including rejection, an unknown id and the 64-command limit), a title
the playlist lacks, a throwing listener and a bridge without a callback.
`fuzz-runtime` drives every event type through a bridge.

**Acceptance:** fake-clock tests prove stable ordering and timer behavior across
pause, seek, rate change, discontinuity, title end, menu close, and reopen.

## Phase 7 — VLC integration

These tasks belong in the VLC source tree unless a small adapter can be tested in
this repository.  Pin the libhddvd API/ABI version in every integration PR.

### P7-01 — Publish consumable build metadata and capabilities

**Status:** Complete
**Depends on:** stable APIs from P1, P3, P4, P5, and P6
**Likely files:** `CMakeLists.txt`, `libhddvd.pc.in`, installed CMake config

Install package metadata, ABI version information, and queryable feature bits for
object envelopes, markup, scripts, rendering, navigation, and protection.

**Acceptance:** clean C and C++ consumer projects build against both installed
shared and static libraries and can report exactly which optional backends exist.

### P7-02 — Implement the VLC access/demux module

**Status:** In progress
**Depends on:** P1-05, P6-02 through P6-04, P7-01

Open an HD DVD root, provide authorized clear streams, map title/EVO ranges to
VLC demuxing, publish chapters and streams, and forward playback state to the
navigation scheduler.

The module is developed in the VLC source tree (`modules/access/hddvd.c`), as
VLC's libdvdnav and libbluray modules are; libhddvd supplies what it calls.

**First slice (done):** `hddvd_titles` (`src/titles.c`) gives a player a
Standard Content disc's titles, chapters and durations from the VMG and title
sets, and reads a title's cells as one MPEG-PS byte stream, refusing
AACS-scrambled packets with `HDDVD_EPROTECTED`. A VLC 4 access module over it
(titles and chapters published to VLC, the stream read by VLC's PS demuxer,
encrypted titles reported in an "Encrypted HD DVD" dialog) builds in VLC's
tree and passes a headless libvlc check over the synthetic
`tests/fixtures/standard` disc; it is submitted to VLC separately, together
with a contrib entry that needs a tagged libhddvd release.

**Second slice (done), library side:**
- **Advanced Content titles:** `hddvd_titles_open_playlist` lists a
  playlist's titles. Each primary clip is an extent of its EVO, found through
  its time map and VTI, and the playlist's chapters get byte offsets.
  `hddvd_open_playlist` finds the disc's playlist (`ADV_OBJ/VPLSTnnn.XPL`,
  highest number). EVO files are opened one at a time, so a playlist with
  hundreds of clips does not hold hundreds of files open.
- **Standard Content navigation:** `hddvd_std_nav` (`src/std_nav.c`) follows
  first play, menus, jumps and links through the command VM and returns the
  cell to play as a title byte range. It also takes the user's title, chapter
  and menu choices, and supports resume. What it does not model ends in an
  explicit step: menu cells, the next/previous/parent PGC links, and commands
  the VM does not run.
- **Tool:** `hddvd-titles` lists both kinds of title, reads them through
  (`--read`) and follows a disc's navigation (`--navigate`).
- **Coverage:** `test_titles_playlist.c` and `test_std_nav.c` cover both on
  synthetic discs, with every rejection. `fuzz-titles` replaces one file of a
  synthetic disc at a time (EVO sectors, title set, VMG, time map, playlist),
  then lists, reads and navigates it.

- **Protected sectors:** `hddvd_titles_set_sector_provider` lets a host that
  is authorized to play a disc supply the clear form of a scrambled sector.
  The result is checked like any stored sector. libhddvd still decrypts
  nothing itself.

**Third slice (done), the VLC module** (prepared as a patch series against
VLC master; it lives in VLC's tree, not here). It is an access-demux like
VLC's Blu-ray module, with a chained PS demuxer:
- Standard Content plays as authored through `hddvd_std_nav` (first play,
  menus that jump, cell links, the user's title, chapter and menu choices,
  and seeks through `hddvd_std_nav_seek`).
- Advanced Content offers the playlist's titles and runs the title's
  application in an `hddvd_menu` session: the display list is drawn as an
  overlay on the video, VLC's navigation keys and the mouse go to it, the
  session's clock follows the title time (playback state reaches the
  scheduler this way), and its player commands are carried out:
  - jumps change the title;
  - pause stops reading until a play command, and the player state is
    reported back with `hddvd_runtime_set_player`;
  - audio and subtitle choices by track number or language select the
    stream that the playlist track's `streamNumber` names;
  - subtitle visibility and the application's z-order are applied;
  - activating or inactivating other applications is refused, because one
    application runs per title.
- Mouse positions are mapped through the size of the video under the
  overlay, which VLC reports to the overlay's updater. The first version
  read the size from the elementary stream's format, which VLC's PS demuxer
  leaves at zero for MPEG-2, so every click missed.
- Track choices needed a third VLC patch: the PS demuxer gives streams their
  stream ids as ES ids when a private `ps-stream-ids` option is set, as
  VLC's DVD modules already do. Without the option nothing changes.
- Headless libvlc checks run against VLC master. They cover:
  - navigation, title and chapter choices, and seeks through the navigator,
    with the disc's navigation continuing from the cell sought;
  - the menu, keys, the overlay on a real video output, and the dialog for
    an encrypted title;
  - mouse clicks on the menu's buttons, through VLC's own video output path
    (a test window plugin reports the events), which pause and play, select
    tracks, hide subtitles, reorder and inactivate the application, and
    jump;
  - with the experimental `--hddvd-guess` option (off by default), which
    turns on `hddvd_std_nav`'s guesses: on `tests/fixtures/guesses`, first
    play, the title menu holding as a still, VLC's navigation keys moving
    between and activating buttons across two menu PGCs, the angle block,
    the PGC links, and the encryption dialog for the menu's scrambled
    packet; on a scratch copy whose title menu has a decodable still, a
    2-second still holding, and the mouse selecting and activating a
    button. Button highlights are not drawn.

**Remaining:**
- in VLC: submitting the merge request, which needs three things from the
  maintainer: a tagged libhddvd release for the contrib entry's checksum, a
  licence notice in the module header, and an account on code.videolan.org.
  VLC has nothing that supplies clear HD DVD sectors, so the module sets no
  sector provider. VLC's contrib builds libhddvd without a script engine,
  so HDi menus need a libhddvd built with QuickJS;
- in VLC: confirming that a playlist track's `streamNumber` (1-based) names
  the EVO substream whose number in the packet id is one less. The module
  assumes so (audio in the low 3 bits of the substream id, subpictures in
  the low 5), as the checks' synthetic disc is built; no disc with several
  audio or subpicture streams has been available to check it against;
- in libhddvd: confirming angle blocks, menu cells and the PGC link numbers
  on a disc. The HD DVD IFO is not DVD-Video's layout (12-byte PGC search
  pointers, 28-byte cells, command offsets at 0xa8 rather than 0xe4;
  `docs/formats/ifo-map.md`), and the one Standard Content disc available
  has none of them: its PGCs link nowhere, its cells form no angle blocks,
  and its menu PGCs have no cells. The specification is licensed, and a
  search found no public description of these structures.

  `hddvd_std_nav_set_options` now implements all three as opt-in guesses
  on DVD-Video's layout, off by default:
  - menu and first-play cells become MENU steps, read through
    `hddvd_std_nav_read_menu` (with the titles' checks and sector
    provider), with buttons from the first navigation pack's highlight
    information, which the host selects and activates;
  - angle blocks play the cell of the angle in SPRM3
    (`hddvd_std_nav_set_angle`); interleaved cells are refused;
  - LinkNextPGC, LinkPrevPGC and LinkGoUpPGC are followed, and a PGC whose
    post commands do not leave it goes on to its next PGC.

  `test_std_nav_guesses.c` covers them, and every refusal, on the synthetic
  `tests/fixtures/guesses` disc. `fuzz-titles` runs the navigator with them
  on and fuzzes the menu EVO, and `hddvd-titles --navigate COUNT --guess`
  shows what they make of a disc. The VLC module turns them on with
  `--hddvd-guess`. Checking them in VLC found that the fixture's first-play
  cell started past the end of its EVO; the fixture now has it at sectors
  8 to 11, and `fp-past-end.IFO` keeps the old layout as a negative case.
  A disc that has these structures is still needed to confirm the offsets.

**Acceptance:** headless tests open synthetic clear layouts; seek/title/chapter
commands land on the expected ranges; protected content produces an actionable
message rather than a generic I/O error.

### P7-03 — Implement VLC overlay and input bridging

**Status:** Complete (the adapter and its mock-host test here; the VLC module in the prepared patch series draws its display lists as an overlay and sends it keys and the mouse, both checked headlessly in VLC)
**Depends on:** P5-03, P6-05, P7-02

Convert display-list changes to VLC subpictures and translate VLC key/mouse/menu
actions to HDi events on a serialized worker.  Queue acknowledgements to avoid
recursive entry into libhddvd.

`hddvd_menu` (`src/menu.c`) is the adapter a player's overlay code drives. It
loads an application from the disc (loose files or an ACA archive) and joins
the script runtime, the scheduler, the runtime bridge, the scene builder and
the display list behind one handle:
- **Input:** keys and scheduler events go through the navigation queue.
  Pointer positions are mapped through the letterbox transform and a hit test
  to the element drawn there (`hddvd_runtime_pointer`, new: the element takes
  the pointer and the focus, and is actioned while the button is down; this
  model is unconfirmed against a player).
- **Output:** `hddvd_menu_update` returns the display list for the current
  state and whether it was rebuilt, with damage against the previous list.
- **Commands:** scripts' player commands are polled, and acknowledgements go
  back through the queue, so nothing re-enters libhddvd.
- **Tests:** `test_menu.c` covers keys, the pointer, damage, resize,
  letterbox, commands, close and reopen, missing and failing parts, and an
  application read from an archive. `test_menu_threads.c` is a mock host:
  input, clock and video threads post to the one worker that owns the
  session, and the CI ThreadSanitizer job reports no races.

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

## Phase 8 — release hardening

The original cross-cutting tasks remain independently reviewable and keep their
stable `X-` task IDs.

### X-03 — CI hardening matrix

**Status:** Complete
**Depends on:** none

Add GCC and Clang builds, ASan/UBSan, a Clang fuzzer compile check, install/consumer
tests, and Windows/macOS jobs where project infrastructure permits.  The external
reference archive remains absent from public CI.

The `fuzzers` job builds every harness and replays the synthetic seeds; the
build matrix covers shared+static, static-only, and shared-only libraries.
The harnesses link `hddvd_fuzz`, a static copy of the library built with
`-fsanitize=fuzzer-no-link`, so libFuzzer's coverage feedback reaches the
parsers. Before this, `fuzz-timemap` reported 3 edges; with it, 445. The first instrumented
run found a heap over-read in the `HDDVDOBJ` envelope parser: a payload length
near 2^64 wrapped the length sum back to the input size. It is fixed, with a
regression test in `test_object.c`.
- **New harnesses:**
  - `fuzz-uri` covers URI parsing and resource resolution against a temporary disc root.
  - `fuzz-titlemap` fuzzes playlists against a synthetic disc that holds a time map and a VTI.
  - `fuzz-display` builds display lists from fuzzed scene nodes and decoder output, including a decoder that reports a too-short stride.
  - Their seeds are generated into `tests/fixtures/fuzz/`.
- **Scheduled fuzzing:** `.github/workflows/fuzz.yml` runs every harness for ten minutes each night, and on demand. It uploads crash inputs as artifacts.

### X-04 — Release checks

**Status:** Complete (the checks are in place and passed on all three systems in GitHub Actions up to run 117; from run 118 on, Actions starts no runner for any job, so later commits are verified by running the Linux jobs locally: the same steps as `ci.yml`, with every job passing on 56eaecd, and the `abi` job's steps, which pass against the merge base with main (25 functions added, none changed or removed); macOS and Windows are unverified since run 117)
**Depends on:** X-03

`ci.yml` adds four release checks:

- **`tsan`:** runs the test suite under ThreadSanitizer. `test_threads.c` uses a separate set of objects in each of four threads at once: playlists, markup and styles, time maps and VTIs, navigation schedulers, and display lists. The results must match a single-threaded run. The library keeps no mutable global state, and ThreadSanitizer reports no races.
- **`static-analysis`:** runs the Clang static analyzer over the library, tools and tests, and fails on any report. Its first run found three issues, all now fixed:
  - in `hddvd_script_close`, a NULL dereference after a failed allocation;
  - in `hddvd-scriptinfo`, `qsort` called on a NULL array;
  - in a test, a dead store.
- **`i386`:** builds and tests a 32-bit library using `cmake/toolchains/linux-i386.cmake`, including the install-consumer test. It builds with no warnings.
- **`abi`:** runs on pull requests. It builds the shared library at the base commit and at the head, and runs `tools/abi_check.sh`, which fails unless the changes are only additions.
  - abidiff's own "incompatible" exit bit is not enough. It stays clear when a public struct changes layout, which `abi_check.sh` catches through the changed-function count. This was verified by inserting a field into `hddvd_xmu_style`.

## Phase 9 — actionable diagnostics

### X-01 — Error detail API

**Status:** Complete
**Depends on:** none

Add structured per-context diagnostics containing status, subsystem, byte offset,
resource URI, and a human-readable message while retaining `hddvd_strerror` for
stable status names.

## Future cross-cutting work

### X-02 — Configurable limits

**Status:** Complete
**Depends on:** P1-01 ownership conventions

Define one limits object for archive members, resource bytes, XML nodes/depth,
decoded pixels, script heap/instructions/recursion, timers, and queued events.
Provide conservative defaults and tests proving every limit is enforced.

`test_limits.c` lowers each field with a consumer through the unified
`*_with_config` entry points and checks the rejection (`max_resource_bytes` is
covered by `test_resource.c`). The script runtime takes `max_script_heap_bytes`,
`max_script_recursion`, `max_script_instructions` (as its operation budget)
and `max_script_bytes` from the config; `test_limits.c` checks each when the
build has an engine.

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
