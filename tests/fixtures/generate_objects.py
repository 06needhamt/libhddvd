#!/usr/bin/env python3
"""Generate original deterministic HDDVDOBJ and clean-room script fixtures."""
from pathlib import Path
import struct

OUT = Path(__file__).resolve().parent / "objects"
SCRIPTS = Path(__file__).resolve().parent / "scripts"
TIMEMAP = Path(__file__).resolve().parent / "timemap"

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

def sectors(blob):
    return blob + b"\0" * (-len(blob) % 2048)

# An original time map for EVOB 1 and the VTI record it must agree with. The
# units are 3003/8 ticks of the 90 kHz clock (docs/formats/ifo-map.md).
TMAP_ENTRIES = [(3, 120, 40), (2, 140, 25), (9, 140, 60), (1, 50, 6)]
TMAP_START = 5000
tmap_units = sum(units for _, units, _ in TMAP_ENTRIES)
tmap_sectors = sum(size for _, _, size in TMAP_ENTRIES)

def tmap():
    head = bytearray(0x1a0)
    head[0:12] = b"HDDVD_TMAP00"
    head[0x72:0x7e] = b"ORIGINAL.VTI"
    head[0x180:0x188] = struct.pack(">IHH", 0x1a0, 1, len(TMAP_ENTRIES))
    body = b"".join(struct.pack(">I", ref << 21 | units << 11 | size)
                    for ref, units, size in TMAP_ENTRIES)
    return sectors(bytes(head) + body)

def vti():
    record = bytearray(0x140)
    record[2:14] = b"ORIGINAL.EVO"
    record[0x108:0x118] = struct.pack(">HIIIH", 1, TMAP_START,
                                      TMAP_START + tmap_units * 3003 // 8, tmap_sectors, 1)
    table = struct.pack(">HHII", 0, 1, 8 + 4 + len(record) - 1, 12) + bytes(record)
    head = bytearray(2048)
    head[0:12] = b"ADVANCED-VTS"
    head[0x1c:0x20] = struct.pack(">I", 1)
    head[0xbc:0xc0] = struct.pack(">I", 1)
    return bytes(head) + sectors(table)

timemaps = {"original.map": tmap(), "original.vti": vti()}

# Seeds for harnesses whose input is not a file format of its own.
FUZZ = Path(__file__).resolve().parent / "fuzz"
fuzz_seeds = {
    "uri/disc-file.txt": b"file:///dvddisc/ADV_OBJ/menu.xmf",
    "uri/archive-member.txt": b"file:///dvddisc/ADV_OBJ/menu.aca/menu.xmu",
    "uri/traversal.txt": b"file:///dvddisc/ADV_OBJ/../../etc/passwd",
    # fuzz-titlemap's disc holds ONE.MAP (40 frames of EVOB 1) and its VTI.
    "titlemap/two-clips.xpl": (
        b"<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        b"<TitleSet timeBase='60fps'><Title id='t' titleDuration='00:00:01:00'>"
        b"<PrimaryAudioVideoClip src='file:///dvddisc/HVDVD_TS/ONE.MAP' "
        b"titleTimeBegin='00:00:00:00' titleTimeEnd='00:00:00:20'/>"
        b"<PrimaryAudioVideoClip src='file:///dvddisc/HVDVD_TS/ONE.MAP' "
        b"titleTimeBegin='00:00:00:20' clipTimeBegin='00:00:00:10' "
        b"titleTimeEnd='00:00:00:40'/></Title></TitleSet></Playlist>"),
    # Viewport 640x360, four 8x8 images, then a root and two image children
    # (20 bytes each: parent, x, y, width, height, then 11 attribute bytes).
    "display/three-nodes.bin": (
        struct.pack(">HH", 640, 360) + bytes([8, 8] * 4) + bytes([0, 16, 16, 0])
        + bytes([0]) + struct.pack(">hhHH", 10, 10, 300, 200) + bytes([0, 0, 0, 0, 0, 255, 0b101, 0, 0, 0, 0])
        + bytes([0]) + struct.pack(">hhHH", 5, 5, 8, 8) + bytes([0, 0, 0, 0, 1, 255, 0b111, 4, 5, 0, 0])
        + bytes([0]) + struct.pack(">hhHH", 20, 5, 8, 8) + bytes([0, 0, 4, 4, 2, 128, 0b101, 6, 0, 0, 0])),
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

TIMEMAP.mkdir(exist_ok=True)
for path in TIMEMAP.iterdir():
    if path.is_file(): path.unlink()
for name, data in sorted(timemaps.items()):
    (TIMEMAP / name).write_bytes(data)

for name, data in sorted(fuzz_seeds.items()):
    path = FUZZ / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
