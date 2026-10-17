# Synthetic parser fixtures

Files in this directory are original, minimal test inputs.  They are not copied
or extracted from the external reference menu.

* `plain_manifest.xmf` exercises the manifest namespace and its resource types.
* `plain_markup.xmu` exercises style/state namespaces, image lists, navigation,
  initial focus, and nested layout.

* `playlist_full.xpl` exercises every construct the typed XPL model represents.
* `objects/` and `scripts/` are written by `generate_objects.py`: envelope
  fixtures (valid and single-field corruptions) and clean-room `HDIS` scripts,
  plus original objects in the on-disc `AACS` envelope layout (clear text,
  clear image, and a protected script).
* `timemap/` is also written by `generate_objects.py`: an original VTI with one
  EVOB record and the time map that agrees with it.
* `fuzz/` holds generated seeds for the harnesses whose input is not a file
  format of its own: URIs, playlists for the title-map harness's synthetic
  disc, and the byte layout `fuzz_display.c` decodes into scene nodes.

They are seeds for the parser fuzzers and will become direct typed-model fixtures
when the Phase 3 APIs are introduced.  `test_aca.c` independently constructs its
ACA container fixture at runtime, including its directory and CRC.
