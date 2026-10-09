# Synthetic parser fixtures

Files in this directory are original, minimal test inputs.  They are not copied
or extracted from the external reference menu.

* `plain_manifest.xmf` exercises the manifest namespace and its resource types.
* `plain_markup.xmu` exercises style/state namespaces, image lists, navigation,
  initial focus, and nested layout.

* `playlist_full.xpl` exercises every construct the typed XPL model represents.
* `objects/` and `scripts/` are written by `generate_objects.py`: envelope
  fixtures (valid and single-field corruptions) and clean-room `HDIS` scripts,
  plus a file carrying only the externally observed `AACS`/`0x11` prefix.

They are seeds for the parser fuzzers and will become direct typed-model fixtures
when the Phase 3 APIs are introduced.  `test_aca.c` independently constructs its
ACA container fixture at runtime, including its directory and CRC.
