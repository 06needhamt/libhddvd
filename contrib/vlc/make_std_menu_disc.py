"""Builds a scratch copy of tests/fixtures/guesses whose title menu cell
(VMG PGC 1) is a VOBU at sector 12 of HV000M01.EVO: the fixture's navigation
pack, with its buttons, then a decodable MPEG-2 still (still.vob), so that
VLC opens a video output and mouse events reach the module. First play's
cell holds for 2 seconds."""
import shutil, struct, sys
from pathlib import Path

fixture, still, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
if out.exists():
    shutil.rmtree(out)
shutil.copytree(fixture / "HVDVD_TS", out / "HVDVD_TS")
evo = bytearray((out / "HVDVD_TS/HV000M01.EVO").read_bytes())
assert len(evo) == 12 * 2048
evo[2 * 2048 + 20] &= 0xcf  # clear the scrambled audio packet
nav = evo[0:2048]
video = still.read_bytes()
assert len(video) % 2048 == 0
evo += nav + video
(out / "HVDVD_TS/HV000M01.EVO").write_bytes(evo)
last = 12 + len(video) // 2048

ifo = bytearray((out / "HVDVD_TS/HV000I01.IFO").read_bytes())
# The title menu's cell: category (0, 0, still 255, 0), its time, then
# first, 0, last VOBU, last sector: 0, 0, 0, 3.
old = bytes([0, 0, 255, 0])
cell = struct.pack(">IIII", 0, 0, 0, 3)
hits = [i for i in range(len(ifo) - 24) if ifo[i:i + 4] == old and ifo[i + 8:i + 24] == cell]
assert len(hits) == 1, hits
ifo[hits[0] + 8:hits[0] + 24] = struct.pack(">IIII", 12, 0, 12, last)
# First play's cell (sectors 8 to 11) gets a still of 2 seconds.
fp = [i for i in range(len(ifo) - 24)
      if ifo[i:i + 4] == bytes(4) and ifo[i + 8:i + 24] == struct.pack(">IIII", 8, 0, 8, 11)]
assert len(fp) == 1, fp
ifo[fp[0] + 2] = 2
(out / "HVDVD_TS/HV000I01.IFO").write_bytes(ifo)
print(out, "title menu sectors 12 to", last)
