# HD DVD demuxer for FFmpeg (libhddvd)

A patch against FFmpeg master (76fa189), for the ffmpeg-devel mailing list.
It is kept here until it is submitted; the demuxer itself belongs in
FFmpeg's tree.

- `0001-avformat-add-HD-DVD-demuxer-using-libhddvd.patch` – the `hddvd`
  demuxer (`libavformat/hddvddec.c`), modelled on FFmpeg's `dvdvideo`
  demuxer. It adds `--enable-libhddvd` (libhddvd 0.6.0 or later, found with
  pkg-config; LGPL 2.1 or later, so it needs no `--enable-gpl`), the demuxer's
  documentation in `doc/demuxers.texi`, a Changelog entry and a libavformat
  minor version bump.

What it does:

- `-f hddvd -i DIR` opens the directory of a mounted disc or of a copy of its
  files (not ISO images or drives). Standard Content titles come from the
  title table, Advanced Content titles from the playlist.
- `-title N` chooses a title (default: the longest with audio or video);
  `-chapter_start` and `-chapter_end` choose a chapter range.
- libhddvd reads the title's cells or clips as one MPEG program stream, and a
  nested MPEG-PS demuxer, which already knows the EVO substreams, parses it.
  It is restarted where the title's data jumps on the disc.
- Each extent's timestamps are anchored onto the title's time, so they are
  continuous across joins, chapter ranges and seeks:
  - for Advanced Content, by the title's time maps (`hddvd_title_map_locate`),
    because a clip can start inside an EVOBU;
  - otherwise, or when the maps disagree with the stream by more than a
    second, by the first video PTS in the PES headers at the extent's start.
- Seeking interpolates within a cell or clip, starting from a chapter's
  offset when one is inside it, and reads from about a second early, so the
  `ffmpeg` tool's `-ss` lands on the requested frame.
- Chapters are exported. Audio and subtitle streams take their language from
  the title set's stream attributes (Standard Content) or from the playlist
  track whose `streamNumber` matches the substream (Advanced Content). That
  mapping has not been confirmed on a disc; the VLC module makes the same
  assumption.
- AACS is not decrypted: the first protected sector ends demuxing with
  `AVERROR(EACCES)` and a message naming AACS.
- Not supported: menus, Advanced Content applications, angles, and secondary
  (picture-in-picture) streams.

## Checks (not part of the patch)

Build libhddvd 0.6.0 and install it. Then build FFmpeg with the patch and
`--enable-libhddvd`. A small build is enough, for example:

```sh
PKG_CONFIG_PATH=$LIBHDDVD_PREFIX/lib/pkgconfig ./configure --disable-everything \
  --enable-libhddvd --enable-demuxer=hddvd,mpegps,mpegvideo,matroska \
  --enable-protocol=file,pipe --enable-muxer=null,matroska \
  --enable-decoder=mpeg2video,ac3 --enable-parser=mpegvideo,ac3 \
  --enable-encoder=rawvideo --enable-filter=null,anull,aresample,scale,format,aformat,signalstats,metadata \
  --extra-ldflags=-Wl,-rpath,$LIBHDDVD_PREFIX/lib
```

- `make_discs.py LIBHDDVD_TREE OUT` builds four decodable synthetic discs.
  It takes the Standard Content layout from libhddvd's fixture generator and
  makes the media with the system `ffmpeg` (MPEG-2 video and two AC-3
  streams, one colour per segment):
  - `ac`: an Advanced Content title of two clips from two EVOs (red, then
    blue) with four chapters, a second title with one clip, and a third
    whose clip starts 1.5 s into its EVO, in the middle of a time map entry.
  - `ac-scrambled`: the same, with a video packet marked scrambled.
  - `ac-badvti`: the same, with VTI start times 5 s off, so the demuxer
    falls back to the PES timestamps.
  - `std`: a Standard Content title whose second cell comes before its first
    on the disc (white, then green), and a second title (yellow).
- `frames.sh FFMPEG_BUILD_DIR DISC [options]` decodes a title. It prints the
  frame count, the frame times and the runs of each colour.
- `run_checks.sh FFMPEG_BUILD_DIR DISCS` runs these cases and diffs the result
  against `checks.expected`:
  - whole titles (including the mid-EVOBU clip and the fallback), chapter
    ranges and seeks;
  - a remux to Matroska (streams, languages, chapters);
  - the scrambled disc;
  - refused inputs.

`REVIEW.md` is a review of the patch against the rules of FFmpeg's review
bot, Fairy, and says what it changed. These checks were also run under ASan
and UBSan with no reports. FFmpeg's default configuration with
`--enable-libhddvd` builds, and `make fate-source` passes.
