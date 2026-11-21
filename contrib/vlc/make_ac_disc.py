#!/usr/bin/env python3
"""Builds a synthetic Advanced Content disc for checking the VLC module:
two 20 s titles from generated MPEG-2 program streams, and a menu
application (loose files) on the first. Its "play" button, pressed with a
key or the mouse, jumps to the second title; its "other" button pauses,
chooses the audio track by number and language, hides subtitles, moves the
application to the top and asks to inactivate it, and plays again when
pressed a second time."""
import shutil, struct, sys
from pathlib import Path

root, media = Path(sys.argv[1]), Path(sys.argv[2])
scramble = len(sys.argv) > 3 and sys.argv[3] == "scramble"
(root / "HVDVD_TS").mkdir(parents=True, exist_ok=True)
(root / "ADV_OBJ").mkdir(parents=True, exist_ok=True)
ENTRIES, UNITS = 20, 240

def tmap(evob, sectors):
    each = sectors // ENTRIES
    sizes = [each] * (ENTRIES - 1) + [sectors - each * (ENTRIES - 1)]
    b = bytearray(2048)
    b[0:12] = b"HDDVD_TMAP00"
    b[0x72:0x7e] = b"HVA00001.VTI"
    b[0x180:0x184] = struct.pack(">I", 0x1a0)
    b[0x185] = evob
    b[0x187] = ENTRIES
    for i, size in enumerate(sizes):
        b[0x1a0 + 4 * i:0x1a4 + 4 * i] = struct.pack(">I", 1 << 21 | UNITS << 11 | size)
    return bytes(b)

evos = []
for number, name in enumerate(("one", "two"), 1):
    data = bytearray((media / f"{name}.vob").read_bytes())
    if scramble and number == 2:
        # Mark a video packet six seconds in (after where the menu jumps
        # to) as scrambled.
        for sector in range(len(data) // 2048 * 6 // 20, len(data) // 2048):
            at = sector * 2048 + 14
            if data[at:at + 4] == b"\x00\x00\x01\xe0" and data[at + 6] & 0xc0 == 0x80:
                data[at + 6] |= 0x10
                break
    (root / "HVDVD_TS" / f"{name.upper()}.EVO").write_bytes(data)
    sectors = len(data) // 2048
    (root / "HVDVD_TS" / f"{name.upper()}.MAP").write_bytes(tmap(number, sectors))
    evos.append((f"{name.upper()}.EVO".encode(), sectors))

vti = bytearray(4096)
vti[0:12] = b"ADVANCED-VTS"
vti[0x1c:0x20] = struct.pack(">I", 1)
vti[0xbc:0xc0] = struct.pack(">I", 1)
t = 2048
vti[t + 3] = 2
vti[t + 4:t + 8] = struct.pack(">I", 16 + 2 * 0x140 - 1)
for i, (name, sectors) in enumerate(evos):
    r = t + 16 + 0x140 * i
    vti[t + 8 + 4 * i:t + 12 + 4 * i] = struct.pack(">I", r - t)
    vti[r + 2:r + 2 + len(name)] = name
    vti[r + 0x109] = i + 1
    start = 1000 * (i + 1)
    vti[r + 0x10a:r + 0x116] = struct.pack(">III", start, start + ENTRIES * UNITS * 3003 // 8, sectors)
(root / "HVDVD_TS" / "HVA00001.VTI").write_bytes(vti)

clip = ("<PrimaryAudioVideoClip src='file:///dvddisc/HVDVD_TS/%s.MAP' titleTimeBegin='00:00:00:00' "
        "titleTimeEnd='00:00:20:00'><Video track='1'/>"
        "<Audio track='1' streamNumber='1' langcode='en' selectable='true'/>"
        "<SubAudio track='1' streamNumber='2' langcode='fr'/></PrimaryAudioVideoClip>")
(root / "ADV_OBJ" / "VPLST000.XPL").write_text(
    "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'><TitleSet timeBase='60fps'>"
    "<Title id='Menu' titleDuration='00:00:20:00'>" + clip % "ONE" +
    "<ApplicationSegment src='file:///dvddisc/ADV_OBJ/menu.xmf' autorun='true' "
    "titleTimeBegin='00:00:00:00' titleTimeEnd='00:00:20:00'/>"
    "<ChapterList><Chapter titleTimeBegin='00:00:00:00'/><Chapter titleTimeBegin='00:00:10:00'/></ChapterList>"
    "</Title><Title id='Feature' titleDuration='00:00:20:00'>" + clip % "TWO" + "</Title>"
    "</TitleSet></Playlist>")
(root / "ADV_OBJ" / "menu.xmf").write_text(
    "<Application xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Manifest'>"
    "<Region x='0' y='0' width='1920' height='1080'/>"
    "<Script src='menu.js'/><Markup src='menu.xmu'/></Application>")
(root / "ADV_OBJ" / "menu.js").write_text(
    "var presses = 0;\n"
    "function go() {\n"
    "  Player.playlist.titles['Feature'].jump('00:00:05:00', false);\n"
    "}\n"
    "function commands() {\n"
    "  if (++presses % 2) {\n"
    "    Player.playlist.pause();\n"
    "    Player.track.selectAudioTrackNumber(1);\n"
    "    Player.track.selectAudioLanguage('en');\n"
    "    Player.subtitle.visible = false;\n"
    "    application.moveToTop();\n"
    "    application.advancedApplications[0].inactivate();\n"
    "  } else\n"
    "    Player.playlist.play();\n"
    "}\n"
    "setMarkupLoadedHandler(function () {\n"
    "  document.getElementById('other').state.focused = true;\n"
    "  addEventListener('go', go, false);\n"
    "  addEventListener('commands', commands, false);\n"
    "});\n")
(root / "ADV_OBJ" / "menu.xmu").write_text(
    "<root xmlns='http://www.dvdforum.org/2005/ihd' xmlns:s='http://www.dvdforum.org/2005/ihd#style'>"
    "<head><timing clock='page'><par>"
    "<cue begin=\"id('play')[state:actioned()]\" dur='1f'><event name='go'/></cue>"
    "<cue begin=\"id('other')[state:actioned()]\" dur='1f'><event name='commands'/></cue>"
    "</par></timing></head><body>"
    "<div id='bar' s:x='0px' s:y='880px' s:width='1920px' s:height='200px' s:backgroundColor='#102030'/>"
    "<button id='other' s:x='200px' s:y='920px' s:width='320px' s:height='120px' "
    "s:backgroundImage=\"url('a.png') url('b.png')\" s:navRight='play'/>"
    "<button id='play' s:x='700px' s:y='920px' s:width='320px' s:height='120px' "
    "s:backgroundImage=\"url('a.png') url('b.png')\" s:navLeft='other'/>"
    "</body></root>")
for name in ("a.png", "b.png"):
    shutil.copy(media / name, root / "ADV_OBJ" / name)
