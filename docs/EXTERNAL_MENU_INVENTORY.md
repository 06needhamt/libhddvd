# External HD DVD menu inventory

## Reproducing the report

`tools/inventory_external_menus.py` accepts an externally supplied ZIP or
extracted directory and a built `hddvd-acaextract`. It emits JSON and uses a
temporary directory for ACA members. Source bytes, XML text, asset names inside
XML, and printable script strings are not copied into the repository. The
external JSON report includes hashes and a short structural script prefix in
hexadecimal, but it is written only to the caller's chosen external path.

```sh
cmake -S . -B build -DBUILD_TESTING=ON \
  -DHDDVD_ENABLE_EXTERNAL_MENU_TESTS=ON
cmake --build build
HDDVD_EXTERNAL_MENU_CORPUS=/outside/the/repository/menus.zip \
HDDVD_EXTERNAL_MENU_REPORT=/tmp/hddvd-menu-inventory.json \
  ctest --test-dir build -R external-menu-corpus --output-on-failure
```

The corpus path may also identify an extracted directory. The test skips when
`HDDVD_EXTERNAL_MENU_CORPUS` is unset. The report has explicit `observations`
and `inferences` objects; consumers must not promote an inference to a format
claim without public specification or independently generated evidence.

## Observations from the October 2026 four-playlist corpus

The external corpus supplied for this work contained four parseable XPL
playlists and 16 parseable ACA archives. The XPL documents consistently used the
DVD Forum playlist namespace. Together they exhibited:

* configuration values for streaming buffers, aperture, default video color,
  and network timeout;
* video, audio, and subpicture media-attribute declarations;
* first-play and numbered titles, multiple primary clips, chapters, and video,
  audio, and subtitle navigation tracks;
* title/application time ranges, clip offsets, scheduled events, pause points,
  `onEnd` references, application segments, playlist applications, and resource
  declarations; and
* `file:` resource references to MAP files and advanced-content applications.

Observed time-shaped attributes were `clipTimeBegin`, `loadingBegin`,
`titleDuration`, `titleTime`, `titleTimeBegin`, and `titleTimeEnd`. Values in this
corpus used four colon-separated numeric fields. The inventory tool records the
shape and occurrence count but does not assert the semantic meaning of the last
field independently of the declared time base.

Fourteen ACA/application families contained parseable XMF, XMU, XSS, or related
XML. Constructs recurring in multiple families included manifest applications,
regions, markup and script references, resources, markup bodies, `div`, button,
style, timing, state, audio/subtitle, and event elements. This recurrence is
evidence for prioritizing generic DOM, ID, style, state, timing, and resource
models; it is not evidence that identically named elements have identical
semantics in every namespace.

Five members had an observed `AACS` prefix with byte 4 equal to `0x11`: four in
one menu family and one in an independent family. The generated report compares
only total length, hashes, the first 32 bytes, big-endian numeric views at two
fixed offsets, and printable-run lengths. It deliberately omits printable text
and assigns no meaning to the numeric fields.

## Inferences and implementation consequences

The repeated playlist structures are sufficient to implement and test the core
P6-02 data model with original synthetic XML. They do not supply the referenced
MAP/IFO files, so P6-03 remains blocked on a binary fixture/specification
inventory. The recurring XMF/XMU constructs make P3-02 implementation ready to
start, but style computation remains dependent on that DOM model.

The second type-`0x11` family improves structural comparison for P4-01. Type
`0x11` payloads were later found to be encrypted rather than compiled: on other
discs, readable scripts are ECMAScript source (`docs/formats/hdi-script.md`).

## Data-handling rules

* Keep the ZIP and every extracted member outside the source and build trees.
* Never commit commercial XML, scripts, images, fonts, audio, hashes of visual
  output, or reconstructed payloads as unit-test fixtures.
* Create small original fixtures that exercise a documented construct without
  copying names, values, ordering, or content from an external title.
* Treat AACS fields, script tables, and XML behavior as unknown unless their
  semantics have an independent source.
* Make external-corpus validation opt-in and skip cleanly when no path is set.
