# HD DVD object envelope (`HDDVDOBJ`)

## Scope and evidence

This note specifies the **synthetic, clear object envelope used by libhddvd's
test corpus**.  It is deliberately not claimed to be the on-disc AACS format:
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

`tests/fixtures/generate_objects.py` emits five original valid objects: a
manifest, markup document, PNG-signature payload, script, and an unknown type.
It also emits single-field corruptions for magic, version, reserved flags,
header length, payload length, embedded-NUL name, nonzero padding, and trailing
length.  Files are emitted in sorted order and contain no timestamps, random
values, or reference-disc bytes, so repeated runs are byte-for-byte identical.

