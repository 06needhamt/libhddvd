# HDi script decoder input

## Scope

The decoder uses a small, endian-stable structural representation for original
clean-room fixtures. It is an engine-neutral boundary for exercising table,
control-flow, and resource-limit handling while the proprietary on-disc HDi
encoding remains undocumented. It must not be interpreted as a claim about the
layout or opcode semantics of commercial HD DVD scripts. Such input remains
unsupported rather than being guessed.

## Structural representation

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
The decoder does not execute code. Runtime/backend selection and commercial HDi
format support therefore remain separate capabilities.

## Byte accounting

`hddvd-scriptinfo FILE` reports every byte of a script object exactly once, in
offset order, as one of:

* `structure`: a field of a format documented in this directory and validated
  by the library (the `HDDVDOBJ` envelope or the layout above);
* `observed`: bytes seen in external media whose meaning is not established
  (currently only the `AACS` prefix and the `0x11` byte after it); and
* `unknown`: everything else, including envelope `unknown_1`, protected or
  non-script payloads, trailers, and unreferenced gaps between tables.

The final line is `summary: size=N structure=A observed=B unknown=C`, and the
tool fails if the three totals do not add up to the input size. Tables that
share bytes are reported as an overlap and counted once. Invalid envelopes or
scripts are rejected by the library parsers before anything is labelled.

For the external reference archive, extract the four type-`0x11` members with
`hddvd-acaextract` to a directory outside the repository and run the tool on
each. Until a public specification or independently generated programs exist,
their bytes after the five-byte prefix are expected to be `unknown`; that
result is the P4-01 inventory, not a gap in the tool. The synthetic fixtures in
`tests/fixtures/scripts` and `tests/fixtures/objects/hdis-script.hdo` pin the
accounting in CTest.
