# HDi scripts

## What discs contain

HDi scripts are ECMAScript source text, not a compiled or binary format. Of the
131 `.js` members in the five ripped discs' archives:

| Container | Files | Content |
|---|---:|---|
| None (bare member) | 27 | UTF-16BE source with a byte-order mark |
| Clear `AACS` envelope, type `0x12` | 99 | The same UTF-16BE source at offset 283 |
| Protected `AACS` envelope, type `0x11` | 5 | Encrypted (about 7.95 bits of entropy per byte) |

All 126 readable files are UTF-16BE with a byte-order mark, and they use only
ECMAScript 3rd-edition language features (no `const`, `let`, `JSON` or array
iteration methods), so an embeddable ECMAScript engine can run them unchanged.
The protected payloads have the entropy of encrypted data rather than the
structure of compiled code; they are most likely the same kind of source,
encrypted. libhddvd never decrypts them (see `aacs-object-envelope.md`).

`hddvd_runtime_evaluate` takes this text as it is, UTF-16 with its byte-order
mark or UTF-8, and runs it in the script runtime
(`docs/decisions/0001-script-engine.md`). All 126 readable scripts compile there
without syntax errors.

What a runtime needs beyond the language is the HDi host API: the player,
application, markup DOM, event, timer, file and network objects the scripts
call. `docs/hdi-host-api.md` lists the parts the readable scripts use, measured
with `tools/inventory_hdi_scripts.py`.

## Clean-room structural decoder

The `HDIS` decoder below predates the finding above, when the only scripts
available were protected. It is a small, endian-stable structural format for
original fixtures and exercises table, control-flow and resource-limit
handling. It is not modelled on anything found on a disc and makes no claim
about commercial content. It stays as a bounded test of the decoder and
inspector tools.

### Structural representation

All integers are unsigned big-endian unless stated otherwise. The 32-byte header
contains `HDIS`, a 16-bit version, header size 32, string/function/instruction
counts, followed by offsets for those three tables. Strings are a 32-bit byte
length and non-NUL bytes (their character encoding is deliberately unspecified).
A function is three 32-bit values: string-table
name index, first instruction, and instruction count. Instructions are eight
bytes: opcode, flags, two reserved zero bytes, and a 32-bit operand (exposed as
signed).

Opcodes `0x10` and `0x11` designate control-flow targets and must reference an
instruction. Opcode `0x20` references a string. Other opcodes remain inspectable
as unknown operations; the decoder deliberately assigns them no behavior.

Every offset, count, function range, branch target, string reference, allocation,
and reserved field is validated before the immutable representation is exposed.
The decoder does not execute code.

### Byte accounting

`hddvd-scriptinfo FILE` reports every byte of a script object exactly once, in
offset order, as one of:

* `structure`: a field of a format documented in this directory and validated
  by the library (the `HDDVDOBJ` or `AACS` envelope, or the layout above);
* `observed`: fields with a consistent size in external media but no
  established meaning (`AACS` bytes 5-6 and clear-type trailers); and
* `unknown`: everything else, including envelope `unknown_1`, protected or
  non-script payloads, trailers, and unreferenced gaps between tables.

The final line is `summary: size=N structure=A observed=B unknown=C`, and the
tool fails if the three totals do not add up to the input size. Tables that
share bytes are reported as an overlap and counted once. Invalid envelopes or
scripts are rejected by the library parsers before anything is labelled.

For the external reference archive, extract the four type-`0x11` members with
`hddvd-acaextract` to a directory outside the repository and run the tool on
each. Type `0x11` objects are protected, so everything except the magic, type,
bytes 5-6 and length (11 bytes) is `unknown`: the payload is encrypted, and
reading it needs decryption, not format research. The five type-`0x11` scripts
from the ripped discs (565 to 45,525 bytes) account this way. The synthetic fixtures in
`tests/fixtures/scripts` and `tests/fixtures/objects/hdis-script.hdo` pin the
accounting in CTest.
