#!/bin/bash
# frames.sh FFMPEG_DIR DISC [demuxer options...]: per decoded video frame,
# its time and a colour name from the plane averages; then a summary.
FF=$1; DISC=$2; shift 2
$FF/ffmpeg -hide_banner -nostats -v info -copyts -f hddvd "$@" -i "$DISC" -map 0:v \
  -vf signalstats,metadata=mode=print \
  -c:v rawvideo -f null - 2>&1 | python3 -c '
import re, sys
frames, cur = [], {}
for line in sys.stdin:
    m = re.search(r"pts_time:([-0-9.]+)", line)
    if m: cur = {"t": float(m.group(1))}; frames.append(cur); continue
    m = re.search(r"signalstats\.(\w)AVG=([0-9.]+)", line)
    if m and frames: cur[m.group(1)] = float(m.group(2)); continue
    if re.search(r"rror|arning|Discard|nvalid", line): print("other:", line.rstrip())
def name(f):
    y, u, v = f.get("Y", 0), f.get("U", 128), f.get("V", 128)
    if y > 200 and u < 100: return "yellow"
    if y > 200: return "white"
    if v > 180: return "red"
    if u > 180: return "blue"
    if y > 150: return "yellow"
    return "green"
runs = []
for f in frames:
    n = name(f)
    if not runs or runs[-1][0] != n: runs.append([n, f["t"], f["t"], 0])
    runs[-1][2] = f["t"]; runs[-1][3] += 1
gaps = [round(b["t"] - a["t"], 4) for a, b in zip(frames, frames[1:])]
print("frames", len(frames), "first", frames[0]["t"] if frames else None,
      "last", frames[-1]["t"] if frames else None,
      "min step", min(gaps) if gaps else None, "max step", max(gaps) if gaps else None)
for r in runs: print("  %-6s %7.3f - %7.3f  %d frames" % tuple(r))
'
