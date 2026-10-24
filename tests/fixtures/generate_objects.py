#!/usr/bin/env python3
"""Generate original deterministic HDDVDOBJ and clean-room script fixtures."""
from pathlib import Path
import struct
import zlib

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
    # One attribute record, which the EVOB names: VC-1 video and one English
    # audio stream.
    attributes = bytearray(1024)
    attributes[2:6] = bytes([0x6b, 0x10, 0xc0, 0])
    attributes[0x0e:0x18] = struct.pack(">H", 1) + bytes([0x1c, 0x04, 0xc4, 0]) + b"en" + bytes(2)
    attribute_table = struct.pack(">HHII", 1, 0, 0x10 + 1024 - 1, 0x10) + bytes(4) + bytes(attributes)
    head = bytearray(2048)
    head[0:12] = b"ADVANCED-VTS"
    head[0x1c:0x20] = struct.pack(">I", 2)
    head[0xb8:0xbc] = struct.pack(">I", 2)
    head[0xbc:0xc0] = struct.pack(">I", 1)
    return bytes(head) + sectors(table) + sectors(attribute_table)

timemaps = {"original.map": tmap(), "original.vti": vti()}

# An original Standard Content disc in the layout of docs/formats/ifo-map.md:
# a title set with two titles (PGC 1: two cells, PGC 2: one), its title EVO
# of nine 4-sector VOBUs of 0.5005 s each, and a VMG naming both titles.
STANDARD = Path(__file__).resolve().parent / "standard"
VOBU_SECTORS, VOBU_TICKS = 4, 15 * 3003
# (evob id, cell id, first VOBU, VOBU count)
STD_CELLS = [(1, 1, 0, 3), (1, 2, 3, 2), (2, 1, 5, 4)]
STD_PGCS = [[0, 1], [2]]  # cells of each PGC

def bcd(value):
    return (value // 10) << 4 | value % 10

def std_time(frames):
    """BCD hh mm ss ff with the observed flags: rate code 2 and the seconds
    byte's top bit."""
    seconds, frames = divmod(frames, 30)
    minutes, seconds = divmod(seconds, 60)
    hours, minutes = divmod(minutes, 60)
    return bytes([bcd(hours), bcd(minutes), 0x80 | bcd(seconds), 0x80 | bcd(frames)])

def std_table(count, body, count_size=2):
    """Count, reserved bytes, end address (last byte), then body."""
    head = struct.pack(">H", count) + bytes(2) + struct.pack(">I", 8 + len(body) - 1)
    return head + body

def std_vts(post_command=bytes([0x30, 0x02, 0, 0, 0, 1, 0, 0])):
    cell_frames = [n * 15 for _, _, _, n in STD_CELLS]
    first = lambda c: STD_CELLS[c][2] * VOBU_SECTORS
    last_vobu = lambda c: (STD_CELLS[c][2] + STD_CELLS[c][3] - 1) * VOBU_SECTORS
    last = lambda c: (STD_CELLS[c][2] + STD_CELLS[c][3]) * VOBU_SECTORS - 1
    # Chapters: title 1 is PGC 1 programs 1 and 2, title 2 is PGC 2.
    ptt = std_table(2, struct.pack(">II", 16, 24) +
                    struct.pack(">HHHH", 1, 1, 1, 2) + struct.pack(">HH", 2, 1))
    pgcs = []
    for number, cells in enumerate(STD_PGCS, 1):
        commands = [bytes([0x71, 0, 0, 1, 0, 0, 0, number])] if number == 1 else []
        post = [post_command]
        cell_commands = [bytes([0x30, 0x06, 0, 0, 0, 1, 0, 0])] if number == 1 else []
        count = len(commands) + len(post) + len(cell_commands)
        command_table = (struct.pack(">HHHI", len(commands), len(post), len(cell_commands),
                                     10 + 8 * count - 1) +
                         b"".join(commands + post + cell_commands))
        program_map = b"".join(struct.pack(">H", k + 1) for k in range(len(cells)))
        playback = b""
        for k, c in enumerate(cells):
            # The second cell of PGC 1 runs cell command 1.
            reference = bytes([0x10, 1, 0, 0]) if (number, k) == (1, 1) else bytes(4)
            playback += (bytes([0x02 if k == 0 else 0x08, 0, 0, 0]) + std_time(cell_frames[c]) +
                         struct.pack(">IIII", first(c), 0, last_vobu(c), last(c)) + reference)
        positions = b"".join(struct.pack(">HBB", STD_CELLS[c][0], 0, STD_CELLS[c][1]) for c in cells)
        offsets = [0xb0]
        for part in (command_table, program_map, playback):
            offsets.append(offsets[-1] + len(part))
        header = (struct.pack(">HH", len(cells), len(cells)) +
                  std_time(sum(cell_frames[c] for c in cells)) + bytes(0xa8 - 8) +
                  struct.pack(">HHHH", *offsets) + bytes(0xb0 - 0xb0))
        pgcs.append(header + command_table + program_map + playback + positions)
    pointers, body = b"", b""
    base = 8 + 12 * len(pgcs)
    for number, pgc in enumerate(pgcs, 1):
        pointers += bytes([0x80, number, 0, 0]) + bytes(4) + struct.pack(">I", base + len(body))
        body += pgc
    pgcit = std_table(len(pgcs), pointers + body)
    # Time maps, one per PGC, unit 1 s: entry k names the VOBU playing at
    # (k + 1) s, accumulating cell durations; the flag marks entries whose
    # next entry is in another cell.
    maps = []
    for cells in STD_PGCS:
        vobus = [(c, STD_CELLS[c][2] + i) for c in cells for i in range(STD_CELLS[c][3])]
        total = len(vobus) * VOBU_TICKS
        entries = [(vobus[(k * 90000) // VOBU_TICKS]) for k in range(1, total // 90000 + 1)]
        words = b""
        for k, (cell, vobu) in enumerate(entries):
            flag = k + 1 == len(entries) or entries[k + 1][0] != cell
            words += struct.pack(">I", vobu * VOBU_SECTORS | (0x80000000 if flag else 0))
        maps.append(bytes([1, 0]) + struct.pack(">H", len(entries)) + words)
    map_offsets, map_body = b"", b""
    for m in maps:
        map_offsets += struct.pack(">I", 8 + 4 * len(maps) + len(map_body))
        map_body += m
    tmapt = std_table(len(maps), map_offsets + map_body)
    cadt = std_table(2, b"".join(struct.pack(">HBBII", e, 0, c, first(k), last(k))
                                 for k, (e, c, _, _) in enumerate(STD_CELLS)))
    vobu_count = sum(n for _, _, _, n in STD_CELLS)
    admap = struct.pack(">I", 4 + 4 * vobu_count - 1) + b"".join(
        struct.pack(">I", v * VOBU_SECTORS) for v in range(vobu_count))
    tables = [ptt, pgcit, tmapt, cadt, admap]
    ifo_sectors = 1 + len(tables)
    evo_sectors = vobu_count * VOBU_SECTORS
    head = bytearray(2048)
    head[0:12] = b"STANDARD-VTS"
    # Stream attributes: MPEG-2 video, one English audio and one English
    # subpicture stream, as on the observed disc.
    head[0x214:0x218] = bytes([0x2b, 0x08, 0xc0, 0])
    head[0x218:0x222] = struct.pack(">H", 1) + bytes([0x1c, 0x04, 0xd4, 0]) + b"en" + bytes([0, 0x1f])
    head[0x25a:0x262] = struct.pack(">H", 1) + bytes([0x21, 0x08]) + b"en" + bytes(2)
    head[0x0c:0x10] = struct.pack(">I", 2 * ifo_sectors + evo_sectors - 1)
    head[0x1c:0x20] = struct.pack(">I", ifo_sectors - 1)
    head[0x20:0x22] = struct.pack(">H", 0x10)
    head[0x80:0x84] = struct.pack(">I", 0x7ff)
    for field, value in ((0xc4, ifo_sectors), (0xc8, 1), (0xcc, 2), (0xd4, 3), (0xe0, 4), (0xe4, 5)):
        head[field:field + 4] = struct.pack(">I", value)
    return bytes(head) + b"".join(sectors(t) for t in tables)

def std_evo():
    """Nine VOBUs: a navigation pack, then packs holding a video, an audio and
    a subpicture packet. The navigation pack's PCI packet has the observed
    general-information layout."""
    out = bytearray()
    for c, (_, _, first_vobu, count) in enumerate(STD_CELLS):
        evob_start = 27000 if c != 1 else 27000 + 3 * VOBU_TICKS
        for i in range(count):
            vobu = first_vobu + i
            start = evob_start + i * VOBU_TICKS
            last_in_evob = (c == 0 and False) or (c == 1 and i == count - 1) or (c == 2 and i == count - 1)
            pack = bytearray(b"\x00\x00\x01\xba\x44\x00\x04\x00\x04\x01\x01\x89\xc3\xf8")
            pack += b"\x00\x00\x01\xbb\x00\x06" + bytes(6)
            pack += b"\x00\x00\x01\xbf" + struct.pack(">H", 0x20) + b"\x01" + bytes(0x1f)
            pci = bytearray(0x3d1)
            pci[1:5] = struct.pack(">I", vobu * VOBU_SECTORS)
            pci[0x0d:0x11] = struct.pack(">I", start)
            pci[0x11:0x15] = struct.pack(">I", start + VOBU_TICKS)
            if last_in_evob:
                pci[0x15:0x19] = struct.pack(">I", start + VOBU_TICKS)
            elapsed = (i * 15)
            pci[0x19:0x1d] = bytes([0, 0]) + std_time(elapsed)[2:]
            pack += b"\x00\x00\x01\xbf" + struct.pack(">H", len(pci)) + pci
            pack += b"\x00\x00\x01\xbe" + struct.pack(">H", 2048 - len(pack) - 6)
            pack += b"\xff" * (2048 - len(pack))
            out += pack
            # Then a video packet, an AC-3 audio packet and a subpicture
            # packet (private stream 1 substreams 0xc0 and 0x20).
            for kind in range(VOBU_SECTORS - 1):
                filler = bytearray(b"\x00\x00\x01\xba\x44\x00\x04\x00\x04\x01\x01\x89\xc3\xf8")
                if kind == 0:
                    filler += b"\x00\x00\x01\xe0\x00\x07\x81\x00\x00\x00\x00\x01\xb3"
                elif kind == 1:
                    filler += b"\x00\x00\x01\xbd\x00\x05\x81\x00\x00\xc0\x01"
                elif kind == 2:
                    filler += b"\x00\x00\x01\xbd\x00\x05\x81\x00\x00\x20\x00"
                filler += b"\x00\x00\x01\xbe" + struct.pack(">H", 2048 - len(filler) - 6)
                filler += b"\xff" * (2048 - len(filler))
                out += filler
    return bytes(out)

def std_vmg():
    titles = b"".join(bytes([0x3c, 1, 0, 0]) + struct.pack(">HHHHI", chapters, 0, 1, number, 8)
                      for number, chapters in ((1, 2), (2, 1)))
    sets = std_table(1, struct.pack(">I", 12) + bytes(4))
    head = bytearray(2048)
    head[0:12] = b"HVDVD-VMG100"
    head[0x0c:0x10] = struct.pack(">I", 7)
    head[0x1c:0x20] = struct.pack(">I", 2)
    head[0xc4:0xc8] = struct.pack(">I", 1)
    head[0xd0:0xd4] = struct.pack(">I", 2)
    return bytes(head) + sectors(std_table(2, titles)) + sectors(sets)

standard = {"HV000I01.IFO": std_vmg(), "HV001I01.IFO": std_vts(), "HV001T01.EVO": std_evo(),
            # LinkCN 9 in PGCs of at most two cells: valid tables, a bad target.
            "bad-link.IFO": std_vts(post_command=bytes([0x20, 0x07, 0, 0, 0, 0, 0, 9]))}

# Original scripts for tools/inventory_hdi_scripts.py, in each container the
# tool reads: bare UTF-16 with a byte-order mark, the clear AACS envelope, and
# a protected envelope it must skip.
HDI_API = Path(__file__).resolve().parent / "hdi-api"
menu_js = """// Player.ignored() in a comment is not a use.
/* neither is document.ignored() */
var panel, count = 0, label = "Player.ignored()";
var pattern = /\\/\\/ not a comment/g;

function onKey(evt) {
    if (evt.keyCode == 1) {
        Player.playlist.titles["main"].jump("00:00:00:00", false);
    }
    evt.preventDefault();
    raise("openPanel");
}

function startup() {
    panel = document.getElementById("panel");
    panel.state.setProperty("focused", "true");
    count = count + Math.max(1, 2);
    application.addEventListener("controller_key_down", onKey, false);
    application.addEventListener("openPanel", onOpen, false);
    application.addEventListener("closePanel", onOpen, false);
    application.addEventListener("never_raised", onOpen, false);
    timer = createTimer("00:00:01:00", TIMER_APPLICATION, startup);
}

setMarkupLoadedHandler(startup);
"""
lib_js = """function raise(type) {
    var evt = createEvent(type);
    evt.initEvent(type, true, true);
    dispatchEvent(evt);
}

function onOpen(evt) {
    var options = { visible: true, depth: 2 };
    shared.total = options.depth;
    trace("opened " + evt.type);
}
var shared = {};
"""
menu_xmu = """<root xmlns="http://www.dvdforum.org/2005/ihd"><head><timing>
<event name="closePanel"/></timing></head><body/></root>
"""
hdi_api = {
    "menu.js": b"\xfe\xff" + menu_js.encode("utf-16-be"),
    "lib.js": aacs(0x12, lib_js.encode("utf-8"), "lib.js.AACS", bytes(4), bytes_5_6=0),
    "locked.js": aacs(0x11, bytes(range(32)), trailer=bytes(8), bytes_5_6=1),
    "menu.xmu": menu_xmu.encode("utf-8"),
}

# An HDi application in an ACA archive, for hddvd-menurun: a manifest, a bare
# UTF-16 script, a script in a clear AACS envelope, and markup with three
# buttons in a row. Pressing Enter on "two" starts a timer that focuses
# "three" one second later; Enter on "three" jumps to the Feature title. The
# playlist runs the application in its Menu title.
APP = Path(__file__).resolve().parent / "app"

def aca(members):
    """An HDDVDACA archive (docs/formats/aca-archive.md) with the members in
    order, each listed with its offset, size and CRC-32."""
    directory_size = sum(46 + len(name) for name, _ in members)
    offset = 32 + directory_size
    header = b"HDDVDACA" + bytes([0, 0x10, 0, 1]) + struct.pack(">H", len(members)) + b"\0\x35"
    header += bytes(32 - len(header))
    directory, data = b"", b""
    for name, blob in members:
        directory += struct.pack(">III", offset, len(blob), zlib.crc32(blob)) + bytes([0xff, len(name)])
        directory += name.encode("ascii") + bytes(32)
        data += blob
        offset += len(blob)
    return header + directory + data

app_xmf = """<?xml version="1.0" encoding="UTF-8"?>
<Application xmlns="http://www.dvdforum.org/2005/HDDVDVideo/Manifest">
  <Region x="0" y="0" width="1920" height="1080"/>
  <Script src="file:///dvddisc/ADV_OBJ/app.aca/lib.js"/>
  <Script src="file:///dvddisc/ADV_OBJ/app.aca/app.js"/>
  <Markup src="file:///dvddisc/ADV_OBJ/app.aca/app.xmu"/>
  <Resource src="file:///dvddisc/ADV_OBJ/app.aca"/>
</Application>
"""
app_lib = """function focus(id) { document.getElementById(id).state.focused = true; }
"""
app_js = """var timer;
function later() { focus("three"); }
function onKey(evt) {
    if (evt.key == 0x0D && evt.target.core.id == "two") {
        timer = createTimer("00:00:01:00", TIMER_APPLICATION, later);
        timer.autoReset = false;
        timer.enabled = true;
    }
    if (evt.key == 0x0D && evt.target.core.id == "three" &&
        Player.playlist.currentTitle.attributes.id == "Menu")
        Player.playlist.titles["Feature"].jump("00:00:01:00", false);
}
setMarkupLoadedHandler(function () {
    focus("one");
    addEventListener("controller_key_down", onKey, false);
});
"""
app_xmu = """<root xmlns="http://www.dvdforum.org/2005/ihd"
      xmlns:style="http://www.dvdforum.org/2005/ihd#style"><body>
<button id="one" style:navRight="two"/><button id="two" style:navRight="three"/>
<button id="three"/></body></root>
"""
app_xpl = """<?xml version="1.0" encoding="UTF-8"?>
<Playlist xmlns="http://www.dvdforum.org/2005/HDDVDVideo/Playlist">
  <TitleSet timeBase="60fps">
    <Title id="Menu" titleDuration="00:01:00:00">
      <ApplicationSegment src="file:///dvddisc/ADV_OBJ/app.aca/app.xmf" autorun="true"
          titleTimeBegin="00:00:00:00" titleTimeEnd="00:01:00:00"/>
    </Title>
    <Title id="Feature" titleDuration="00:10:00:00"/>
  </TitleSet>
</Playlist>
"""
app = {
    "app.xpl": app_xpl.encode("utf-8"),
    "app.aca": aca([
        ("app.xmf", app_xmf.encode("utf-8")),
        ("lib.js", aacs(0x12, b"\xfe\xff" + app_lib.encode("utf-16-be"), "lib.js.AACS",
                        bytes(4), bytes_5_6=0)),
        ("app.js", b"\xfe\xff" + app_js.encode("utf-16-be")),
        ("app.xmu", app_xmu.encode("utf-8")),
    ]),
}

# Seeds for harnesses whose input is not a file format of its own.
FUZZ = Path(__file__).resolve().parent / "fuzz"
fuzz_seeds = {
    # fuzz-runtime evaluates a script, then calls its function f.
    "runtime/calls.js": (
        "var count = 0;\n"
        "function f() { for (var i = 0; i < 10; i++) count += helper(i); }\n"
        "function helper(n) { return n % 2 ? Math.floor(Math.random() * 3) : n; }\n"
    ).encode("utf-8"),
    "runtime/utf16.js": b"\xfe\xff" + (
        "function f() { var s = '\u00e9'; if (s.length !== 1) throw new Error(s); }"
    ).encode("utf-16-be"),
    "runtime/throws.js": b"function f() { throw new TypeError('original'); }",
    "runtime/recursion.js": b"function f() { return f(); }",
    "runtime/loop.js": b"function f() { for (;;) {} }",
    "runtime/syntax.js": b"function f( {",
    # The player objects, against fuzz_runtime.c's playlist.
    "runtime/player.js": (
        "var t = Player.playlist.titles;\n"
        "addEventListener('play_state', function (e) {\n"
        "  if (e.newValue == Player.playlist.PLAYSTATE_PAUSE) Player.playlist.play(); }, false);\n"
        "function f() { t.Feature.chapters[1].jump('00:00:01:00', false); t.Menu.jump('00:00:00:00');\n"
        "  Player.track.selectAudioLanguage('en'); Player.subtitle.visible = false;\n"
        "  application.advancedApplications[1].activate(); application.moveToTop();\n"
        "  Player.generalParameters.setValue('k', Player.playlist.currentTitle.elapsedTime);\n"
        "  Diagnostics.trace(Player.track.selectedAudioLanguageCode); }\n"
    ).encode("utf-8"),
    # Player events through the script bridge, making commands as they arrive.
    "runtime/bridge.js": (
        "var n = 0;\n"
        "function on(e) { n++; var t = Player.playlist.currentTitle;\n"
        "  if (t && e.type == 'chapter') t.chapters[0].jump('00:00:00:01');\n"
        "  if (e.type == 'title_end') Player.playlist.pause();\n"
        "  if (e.type == 'audio_track') Player.track.selectSubtitleTrackNumber(n % 3 + 1); }\n"
        "var names = ['title_begin', 'title_end', 'chapter', 'play_state', 'audio_track',\n"
        "  'subtitle_track', 'controller_key_down'];\n"
        "for (var i = 0; i < names.length; i++) addEventListener(names[i], on, false);\n"
        "var t = createTimer('00:00:00:01', 1, function () { Player.playlist.play(); });\n"
        "t.enabled = true;\n"
    ).encode("utf-8"),
    # The host objects, against fuzz_runtime.c's document.
    "runtime/host.js": (
        "var a = document.getElementById('a'), log = [];\n"
        "addEventListener('controller_key_down', function (e) {\n"
        "  log.push(e.key + e.target.core.id); a.style.x = (e.key * 2) + 'px'; }, false);\n"
        "addEventListener('fuzz', { handleEvent: function (e) { throw e; } }, true);\n"
        "setMarkupLoadedHandler(function () {\n"
        "  document.b.style.animateProperty('opacity', '0;1;0.5', 0.5);\n"
        "  var t = createTimer('00:00:00:20', TIMER_APPLICATION, function () {\n"
        "    document.field.state.value = log.join(); a.state.focused = 'true'; });\n"
        "  t.enabled = true; });\n"
        "function f() { var e = createEvent('x'); e.initEvent('fuzz', true, true);\n"
        "  dispatchEvent(e); a.style.setProperty('navRight', 'field'); return a.parentNode; }\n"
    ).encode("utf-8"),
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
    # Every style detail property, keyword and unknown property included.
    "xmu/style-details.xmu": (
        b"<root xmlns='http://www.dvdforum.org/2005/ihd' "
        b"xmlns:s='http://www.dvdforum.org/2005/ihd#style'><head><styling>"
        b"<style id='base' s:opacity='0.4' s:color='#f7dc89' s:font='Original.ttf' "
        b"s:fontSize='24px' s:padding='5px' s:navLeftUp='b' s:backgroundRepeat='no-repeat'/>"
        b"</styling></head><body><div id='a' style='base' s:opacity='inherit' "
        b"s:visibility='hidden' s:backgroundColor='rgba(199,180,153,128)' "
        b"s:fontStyle='oblique' s:textAlign='center' s:displayAlign='after' "
        b"s:wrapOption='wrap' s:textAltitude='140%' s:anchor='center' s:paddingStart='36px' "
        b"s:border='1px solid white' s:backgroundPositionHorizontal='-6px' "
        b"s:backgroundPositionVertical='30%' s:backgroundFrame='2' s:crop='0 0 20 33' "
        b"s:contentWidth='90%' s:contentHeight='scale-to-fit' s:navIndex='none' "
        b"s:navRightDown='none'/><div id='b' s:fontSize='small' s:color='blue' "
        b"s:futureProperty='1'/></body></root>"),
    # A scene with a fill, text, an image object and placed images.
    "xmu/scene.xmu": (
        b"<root xmlns='http://www.dvdforum.org/2005/ihd' "
        b"xmlns:s='http://www.dvdforum.org/2005/ihd#style' "
        b"xmlns:st='http://www.dvdforum.org/2005/ihd#state'><body>"
        b"<div id='panel' s:width='200px' s:height='100px' s:backgroundColor='#102030' "
        b"s:opacity='0.5' s:backgroundImage=\"url('a.png') url('ab.png')\" "
        b"s:backgroundPositionHorizontal='center' s:backgroundPositionVertical='30%'>"
        b"<p s:color='white' s:fontSize='30px'>Original text</p>"
        b"<button id='b' s:width='50%' s:opacity='inherit' "
        b"s:backgroundImage=\"url('abc.png')\" s:contentWidth='50%'/>"
        b"<input mode='display' st:value='OK'/>"
        b"<object type='image/png' src='abcd.png' s:width='8px' s:height='8px'/>"
        b"</div></body></root>"),
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

HDI_API.mkdir(exist_ok=True)
for path in HDI_API.iterdir():
    if path.is_file(): path.unlink()
for name, data in sorted(hdi_api.items()):
    (HDI_API / name).write_bytes(data)

STANDARD.mkdir(exist_ok=True)
for path in STANDARD.iterdir():
    if path.is_file(): path.unlink()
for name, data in sorted(standard.items()):
    (STANDARD / name).write_bytes(data)

APP.mkdir(exist_ok=True)
for name, data in sorted(app.items()):
    (APP / name).write_bytes(data)

for name, data in sorted(fuzz_seeds.items()):
    path = FUZZ / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
