#!/usr/bin/env python3
"""Builds decodable synthetic HD DVD discs for checking FFmpeg's hddvd
demuxer (not part of any patch).

  make_discs.py LIBHDDVD_TREE OUT

OUT/ac         Advanced Content: title 1 "Feature" plays clip ONE (0-4 s)
               then clip TWO (4-8 s), from two EVOs, with chapters at 0, 2,
               4 and 6 s and audio tracks 1 (en) and 2 (fr); title 2 "Extra"
               plays clip TWO alone; title 3 "Late" plays clip ONE from 1.5 s
               into it (in the middle of a time map entry), for 2.5 s. The
               VTI gives each EVO's real first video PTS as its start time.
OUT/ac-scrambled  the same with a video packet of TWO marked scrambled.
OUT/ac-badvti  the same with VTI start times 5 s off, so that the time maps
               do not match the EVOs' timestamps.
OUT/std        Standard Content, the layout of libhddvd's tests/fixtures/
               standard with larger VOBUs, whose title 1 plays cell 2
               (1 s, white) and then cell 1 (1.5 s, green), which comes
               before it on the disc; title 2 plays cell 3 (2 s, yellow).

Every segment is an MPEG-2 video + two AC-3 stream program stream made with
the system ffmpeg, in a colour of its own, padded with padding packs."""
import shutil, struct, subprocess, sys, tempfile
from pathlib import Path

tree, out = Path(sys.argv[1]), Path(sys.argv[2])
work = Path(tempfile.mkdtemp(prefix="hddvd-ffcheck-"))
PAD = b"\x00\x00\x01\xba\x44\x00\x04\x00\x04\x01\x01\x89\xc3\xf8"
PAD += b"\x00\x00\x01\xbe" + struct.pack(">H", 2048 - len(PAD) - 6)
PAD += b"\xff" * (2048 - len(PAD))

def segment(name, seconds, colour, sectors=None):
    path = work / f"{name}.vob"
    subprocess.run(["ffmpeg", "-v", "error", "-y",
                    "-f", "lavfi", "-i", f"color=c={colour}:s=720x480:r=30000/1001:d={seconds}",
                    "-f", "lavfi", "-i", f"sine=f=440:d={seconds}",
                    "-f", "lavfi", "-i", f"sine=f=660:d={seconds}",
                    "-map", "0", "-map", "1", "-map", "2",
                    "-c:v", "mpeg2video", "-g", "15", "-b:v", "1M", "-minrate", "1M", "-maxrate", "1M",
                    "-bufsize", "1M", "-c:a", "ac3", "-b:a", "96k",
                    "-f", "vob", "-packetsize", "2048", str(path)], check=True)
    data = path.read_bytes()
    assert len(data) % 2048 == 0
    if sectors is not None:
        assert len(data) // 2048 <= sectors, (name, len(data) // 2048, sectors)
        data += PAD * (sectors - len(data) // 2048)
    return bytearray(data)

def first_video_pts(data):
    """The PTS of the first video PES packet in an MPEG-2 program stream."""
    for at in range(0, len(data), 2048):
        p = at + 14 + (data[at + 13] & 7)
        while p + 14 <= at + 2048 and data[p:p + 3] == b"\0\0\1":
            sid, length = data[p + 3], struct.unpack(">H", data[p + 4:p + 6])[0]
            if 0xe0 <= sid <= 0xef and data[p + 7] & 0x80:
                b = data[p + 9:p + 14]
                return ((b[0] >> 1) & 7) << 30 | (b[1] << 7 | b[2] >> 1) << 15 | (b[3] << 7 | b[4] >> 1)
            p += 6 + length
    raise ValueError("no video PTS")

def ac_disc(root, scramble, vti_error=0):
    ENTRIES, UNITS = 4, 240  # four entries of 240 * 3003 / 8 ticks, about 1 s each
    (root / "HVDVD_TS").mkdir(parents=True)
    (root / "ADV_OBJ").mkdir(parents=True)

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
    for number, (name, colour) in enumerate((("one", "red"), ("two", "blue")), 1):
        data = segment(name, 4, colour)
        if scramble and number == 2:
            for sector in range(len(data) // 2048 // 2, len(data) // 2048):
                at = sector * 2048 + 14
                if data[at:at + 4] == b"\x00\x00\x01\xe0" and data[at + 6] & 0xc0 == 0x80:
                    data[at + 6] |= 0x10
                    break
        (root / "HVDVD_TS" / f"{name.upper()}.EVO").write_bytes(data)
        sectors = len(data) // 2048
        (root / "HVDVD_TS" / f"{name.upper()}.MAP").write_bytes(tmap(number, sectors))
        evos.append((f"{name.upper()}.EVO".encode(), sectors, first_video_pts(data) + vti_error))

    vti = bytearray(4096)
    vti[0:12] = b"ADVANCED-VTS"
    vti[0x1c:0x20] = struct.pack(">I", 1)
    vti[0xbc:0xc0] = struct.pack(">I", 1)
    t = 2048
    vti[t + 3] = 2
    vti[t + 4:t + 8] = struct.pack(">I", 16 + 2 * 0x140 - 1)
    for i, (name, sectors, start) in enumerate(evos):
        r = t + 16 + 0x140 * i
        vti[t + 8 + 4 * i:t + 12 + 4 * i] = struct.pack(">I", r - t)
        vti[r + 2:r + 2 + len(name)] = name
        vti[r + 0x109] = i + 1
        vti[r + 0x10a:r + 0x116] = struct.pack(">III", start, start + ENTRIES * UNITS * 3003 // 8,
                                               sectors)
    (root / "HVDVD_TS" / "HVA00001.VTI").write_bytes(vti)

    def clip(name, begin, end, clip_begin="00:00:00:00"):
        return ("<PrimaryAudioVideoClip src='file:///dvddisc/HVDVD_TS/%s.MAP' "
                "titleTimeBegin='00:00:%s' titleTimeEnd='00:00:%s' clipTimeBegin='%s'><Video track='1'/>"
                "<Audio track='1' streamNumber='1' langcode='en'/>"
                "<Audio track='2' streamNumber='2' langcode='fr'/>"
                "</PrimaryAudioVideoClip>") % (name, begin if ":" in str(begin) else "%02d:00" % begin,
                                               end if ":" in str(end) else "%02d:00" % end, clip_begin)

    (root / "ADV_OBJ" / "VPLST000.XPL").write_text(
        "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
        "<TitleSet timeBase='60fps'>"
        "<Title id='Feature' titleDuration='00:00:08:00'>" + clip("ONE", 0, 4) + clip("TWO", 4, 8) +
        "<ChapterList><Chapter titleTimeBegin='00:00:00:00'/><Chapter titleTimeBegin='00:00:02:00'/>"
        "<Chapter titleTimeBegin='00:00:04:00'/><Chapter titleTimeBegin='00:00:06:00'/>"
        "</ChapterList></Title>"
        "<Title id='Extra' titleDuration='00:00:04:00'>" + clip("TWO", 0, 4) + "</Title>"
        "<Title id='Late' titleDuration='00:00:02:30'>" + clip("ONE", 0, "02:30", "00:00:01:30") +
        "</Title>"
        "</TitleSet></Playlist>")

def std_disc(root):
    # The fixture generator's Standard Content functions, with title 1's
    # cells played in reverse.
    source = (tree / "tests" / "fixtures" / "generate_objects.py").read_text()
    head = source[source.index("def sectors("):source.index("def tmap(")]
    body = source[source.index("STANDARD = Path("):source.index("standard = {")]
    env = {"struct": struct, "Path": Path, "__file__": str(work / "x.py")}
    exec(head, env)
    exec(body
             .replace("STD_PGCS = [[0, 1], [2]]", "STD_PGCS = [[1, 0], [2]]"), env)
    # On the disc: cell 1 (VOBUs 0-2), cell 2 (3-4), cell 3 (5-8); VOBUs as
    # large as the largest constant-rate segment needs, so that cells are
    # almost all data, as on a disc.
    parts = [segment("cell1", 1.5, "green"), segment("cell2", 1, "white"),
             segment("cell3", 2, "yellow")]
    v = max(-(-len(p) // 2048 // n) for p, n in zip(parts, (3, 2, 4)))
    env["VOBU_SECTORS"] = v
    (root / "HVDVD_TS").mkdir(parents=True)
    (root / "HVDVD_TS" / "HV000I01.IFO").write_bytes(env["std_vmg"]())
    (root / "HVDVD_TS" / "HV001I01.IFO").write_bytes(env["std_vts"]())
    evo = b"".join(p + PAD * (n * v - len(p) // 2048) for p, n in zip(parts, (3, 2, 4)))
    (root / "HVDVD_TS" / "HV001T01.EVO").write_bytes(evo)

if out.exists():
    shutil.rmtree(out)
ac_disc(out / "ac", False)
ac_disc(out / "ac-scrambled", True)
ac_disc(out / "ac-badvti", False, 5 * 90000)
std_disc(out / "std")
shutil.rmtree(work)
