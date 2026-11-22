#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Tom Needham
# run_checks.sh FFMPEG_BUILD_DIR DISCS_DIR
# Runs the demuxer over the discs make_discs.py builds and compares what it
# prints with checks.expected (next to this script). FFMPEG_BUILD_DIR holds
# the ffmpeg and ffprobe binaries of a build with --enable-libhddvd (and the
# mpegvideo raw demuxer, mpeg2video and ac3 decoders, signalstats and
# metadata filters, rawvideo encoder, null muxer, and matroska muxer and
# demuxer).
set -u
FF=$1; D=$2; HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
{
  frames() { echo "== frames $*"; d=$1; shift; "$HERE/frames.sh" "$FF" "$D/$d" "$@" 2>&1 |
             grep -v 'P-frame\|MVs not\|concealing'; }
  frames ac
  frames ac -title 2
  frames ac -title 3
  frames ac-badvti
  frames ac-badvti -title 3
  frames std
  frames std -title 2
  frames ac -chapter_start 2 -chapter_end 3
  frames std -chapter_start 2
  frames std -chapter_end 1
  for t in 0.3 1.0 3.9 5.0 6.5 7.9; do frames ac -ss $t; done
  for t in 0.5 1.2 2.3; do frames std -ss $t; done
  for d in ac std; do
    echo "== remux $d"
    "$FF/ffmpeg" -nostdin -v error -f hddvd -i "$D/$d" -map 0 -c copy -y "$OUT/$d.mkv"
    echo "exit $?"
    "$FF/ffprobe" -v error -show_entries format=duration:stream=codec_name:stream_tags=language:chapter=start_time,end_time \
      -of compact "$OUT/$d.mkv"
  done
  echo "== scrambled"
  "$FF/ffmpeg" -nostdin -v error -f hddvd -i "$D/ac-scrambled" -map 0 -c copy -f null - 2>&1 |
    sed 's/^\[[^]]*\] //'
  echo "exit ${PIPESTATUS[0]} (the ffmpeg tool ends a file on a demuxing error; -xerror fails)"
  "$FF/ffmpeg" -nostdin -v quiet -xerror -f hddvd -i "$D/ac-scrambled" -map 0 -c copy -f null -
  echo "exit $? with -xerror"
  for args in "-i $D" "-i $D/missing" "-title 4 -i $D/ac" "-chapter_start 5 -i $D/ac" \
              "-chapter_start 3 -chapter_end 2 -i $D/ac" "-title 2 -chapter_start 2 -i $D/ac"; do
    echo "== refused $args" | sed "s|$D|DISCS|g"
    "$FF/ffprobe" -v error -f hddvd $args 2>&1 | sed "s/^\[[^]]*\] //; s|$D|DISCS|g"
    echo "exit ${PIPESTATUS[0]}"
  done
} > "$OUT/actual" 2>&1
if diff -u "$HERE/checks.expected" "$OUT/actual"; then echo "all checks match"; rm -rf "$OUT"; exit 0; fi
echo "differences above (actual output kept in $OUT/actual)"; exit 1
