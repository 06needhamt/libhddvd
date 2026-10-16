# HD DVD object envelope (`HDDVDOBJ`)

## Scope and evidence

This note specifies the **synthetic, clear object envelope used by libhddvd's
test corpus**, followed by the observed on-disc `AACS` envelope.  The
synthetic layout is deliberately not claimed to be the on-disc format:
the project has no authoritative public specification or redistributable sample
for that format.  Fields whose meaning has not been established remain named
`unknown_1`.  The only confirmed semantics here are those implemented by the
generator and parser.  Observations of an externally supplied conformance
archive must be recorded separately and must not be used to infer encryption
keys or circumvent access controls.

## Version 1 layout

All integers are unsigned, big-endian.  Offsets below are bytes from the start.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 8 | magic | ASCII `HDDVDOBJ` |
| 8 | 1 | version | `1` |
| 9 | 1 | flags | bit 0 means protected; all other bits are reserved |
| 10 | 2 | type | 1 manifest, 2 markup, 3 script, 4 image; other values are retained |
| 12 | 4 | header length | payload offset, at least 32 |
| 16 | 8 | payload length | stored payload byte count |
| 24 | 2 | name length | byte count of the optional UTF-8-compatible name (no NUL) |
| 26 | 2 | trailer length | opaque trailer byte count |
| 28 | 4 | `unknown_1` | generated as zero and exposed without interpretation |
| 32 | variable | name | exactly `name length` bytes |
| after name | variable | padding | zero bytes through `header length` |
| header length | variable | payload | clear or provider-defined protected bytes |
| after payload | variable | trailer | opaque metadata |

The declared object length is `header length + payload length + trailer
length`, and strict readers require it to equal the input length.  A protected
payload is never interpreted without an explicit provider.  The trailer is
always retained as opaque data.

## Generated corpus

`tests/fixtures/generate_objects.py` emits seven original valid objects: a
manifest, markup document, PNG-signature payload, source-text script, clean-room
`HDIS` script, protected script (flag bit 0), and an unknown type. It also
writes clean-room scripts to `tests/fixtures/scripts` (see `hdi-script.md`).
It also emits single-field corruptions for magic, version, reserved flags,
header length, payload length, embedded-NUL name, nonzero padding, and trailing
length.  Files are emitted in sorted order and contain no timestamps, random
values, or reference-disc bytes, so repeated runs are byte-for-byte identical.


## Observed on-disc `AACS` envelope

The layout below comes from 1,647 objects on five externally supplied discs
(October 2026): 1,643 members of 17 ACA archives plus four playlist files. It
is an observation of those discs, not a specification, and no disc bytes are
kept in the repository. `hddvd_object_open` reads it, and
`hddvd_object_get_format` reports `HDDVD_OBJECT_FORMAT_AACS`.

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 0 | 4 | ASCII `AACS` | all samples |
| 4 | 1 | type byte | `0x01`, `0x02`, `0x11` and `0x12` seen |
| 5 | 2 | unknown (exposed as `unknown_1`) | 0 for `0x12`; 22 or 1 for the others |
| 7 | 4 | payload length, big-endian | for clear types, matches where the payload signature starts and ends |
| 11 | 272 | name and padding, or opaque | see below |
| 283 | length | payload | |
| 283 + length | rest | trailer | |

**Clear types.** Type `0x02` (1,367 PNG members) and type `0x12` (180
members: `.xpl`, `.xmf`, `.xmu`, `.xml`, `.xss`, `.xul`, `.js`) hold the clear
payload at offset 283. Every sample has a printable, NUL-terminated name at
offset 11 (usually the member name plus `.AACS`) followed by zeros, and a
trailer of exactly 16 bytes (`0x02`) or 4 bytes (`0x12`). The trailers are not
CRC-32, MD5, or truncated SHA-1 of the payload, so their meaning is unknown.
The parser requires all of these properties for the clear types. Type `0x12`
is reported as `HDDVD_OBJECT_TEXT`, since the type byte does not say which
kind of text it is.

**Protected types.** Types `0x01` (`.wav`, `.png`, `.ttf`) and `0x11` (`.js`)
have near-random payloads (about 7.95 bits of entropy per byte, with no file
signatures) and non-zero header bytes through offset 282. Type `0x01` had no
trailer; type `0x11` trailers ranged from 8 to 356 bytes. The parser treats
these types, and any unknown type byte, as protected: there is no name,
`info.flags` has the protected bit set, and opening fails with
`HDDVD_EPROTECTED` unless the host supplies a decryption provider. No key
handling or decryption is implemented.
