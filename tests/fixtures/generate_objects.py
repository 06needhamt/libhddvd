#!/usr/bin/env python3
"""Generate original deterministic HDDVDOBJ and clean-room script fixtures."""
from pathlib import Path
import struct

OUT = Path(__file__).resolve().parent / "objects"
SCRIPTS = Path(__file__).resolve().parent / "scripts"

def envelope(kind, payload, name="", trailer=b"", flags=0, unknown_1=0):
    encoded = name.encode("utf-8")
    header_len = (32 + len(encoded) + 3) & ~3
    header = struct.pack(">8sBBHIQHHI", b"HDDVDOBJ", 1, flags, kind,
                         header_len, len(payload), len(encoded), len(trailer),
                         unknown_1)
    return header + encoded + bytes(header_len - 32 - len(encoded)) + payload + trailer

def hdis(strings, functions, instructions, gap=b""):
    """Clean-room script layout from docs/formats/hdi-script.md. `gap` places
    unreferenced bytes between the string and function tables."""
    string_table = b"".join(struct.pack(">I", len(value)) + value for value in strings)
    string_offset = 32
    function_offset = string_offset + len(string_table) + len(gap)
    function_table = b"".join(struct.pack(">III", *entry) for entry in functions)
    instruction_offset = function_offset + len(function_table)
    instruction_table = b"".join(struct.pack(">BBHi", opcode, flags, 0, operand)
                                 for opcode, flags, operand in instructions)
    header = struct.pack(">4sHHIIIIII", b"HDIS", 1, 32, len(strings), len(functions),
                         len(instructions), string_offset, function_offset,
                         instruction_offset)
    return header + string_table + gap + function_table + instruction_table

def aacs(kind, payload, name=None, trailer=b"", bytes_5_6=0x16):
    """Original envelopes in the AACS layout of docs/formats/aacs-object-envelope.md.
    Without a name, the header area after byte 11 is filled with a pattern, as
    protected objects have no readable name there."""
    if name is None:
        area = bytes((i * 7 + 1) & 0xff for i in range(11, 283))
    else:
        area = name.encode("ascii") + bytes(272 - len(name))
    header = b"AACS" + bytes([kind]) + struct.pack(">HI", bytes_5_6, len(payload)) + area
    return header + payload + trailer

minimal_script = hdis([b"main"], [(0, 0, 2)], [(0x20, 0, 0), (0x10, 0, 0)])
scripts = {
    "minimal.hdis": minimal_script,
    "gapped.hdis": hdis([b"init", b"focus"], [(0, 0, 1), (1, 1, 2)],
                        [(0x20, 0, 1), (0x7f, 0, 0), (0x11, 0, 0)], gap=bytes(4)),
    # Protected AACS layout: opaque header area, payload, and trailer.
    "aacs-protected.bin": aacs(0x11, bytes(range(64)), trailer=bytes(12), bytes_5_6=1),
}

valid = {
    "hdis-script.hdo": envelope(3, minimal_script, "main.hds", b"TAIL"),
    "protected.hdo": envelope(3, b"provider-defined", "locked.hds", flags=1),
    "aacs-text.hdo": aacs(0x12, b'<Application id="original"/>', "app.xmf.AACS",
                          bytes(4), bytes_5_6=0),
    "aacs-image.hdo": aacs(0x02, b"\x89PNG\r\n\x1a\n", "focus.png.AACS", bytes(16)),
    "image.hdo": envelope(4, b"\x89PNG\r\n\x1a\n", "focus.png"),
    "manifest.hdo": envelope(1, b'<Application id="original"/>', "manifest.xmf", b"META"),
    "markup.hdo": envelope(2, b"<root/>", "main.xmu"),
    "script.hdo": envelope(3, b"function original() {}\n", "main.js"),
    "unknown.hdo": envelope(0x1234, b"opaque"),
}

def mutate(blob, offset, replacement):
    return blob[:offset] + replacement + blob[offset + len(replacement):]

base = valid["manifest.hdo"]
invalid = {
    "bad-flags.hdo": mutate(base, 9, b"\x80"),
    "bad-header-length.hdo": mutate(base, 12, struct.pack(">I", 31)),
    "bad-magic.hdo": mutate(base, 0, b"X"),
    "bad-name-nul.hdo": mutate(base, 32, b"\0"),
    "bad-padding.hdo": mutate(envelope(1, b"x", "x"), 33, b"X"),
    "bad-payload-length.hdo": mutate(base, 16, struct.pack(">Q", 999)),
    "bad-trailer-length.hdo": mutate(base, 26, struct.pack(">H", 999)),
    "bad-version.hdo": mutate(base, 8, b"\x02"),
}

OUT.mkdir(exist_ok=True)
for path in OUT.iterdir():
    if path.is_file(): path.unlink()
for name, data in sorted({**valid, **invalid}.items()):
    (OUT / name).write_bytes(data)

SCRIPTS.mkdir(exist_ok=True)
for path in SCRIPTS.iterdir():
    if path.is_file(): path.unlink()
for name, data in sorted(scripts.items()):
    (SCRIPTS / name).write_bytes(data)
