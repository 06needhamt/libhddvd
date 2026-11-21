# Review of "avformat: add HD DVD demuxer using libhddvd" against Fairy's rules

Fairy (code.ffmpeg.org/michaelni/Fairies) could not be run as FFmpeg runs
it. Its reviewer calls an LLM provider (the repository's reference FFmpeg
launcher, `fairy-ref.sh`, uses OpenAI `gpt-5.6@high` with repository and
web search), and no key was used. This is
a manual review instead, applying Fairy's own reviewer prompt
(`llm_prompt.py`: its general rules, code and design reviewer roles, issue
severities and classifications) and FFmpeg's project facts
(`project_facts/ffmpeg.md`). It is not the bot's verdict.

<!-- Scope: exhaustive over the one commit (libavformat/hddvddec.c, configure,
Makefile, allformats.c, version.h, doc/demuxers.texi, Changelog): every
function read; claims checked against FFmpeg's sources (demux.c, mpeg.c,
dvdvideodec.c, configure), libhddvd's header, and builds and runs on
synthetic discs. Not covered: real HD DVD discs, MSVC itself (no MSVC
here), H.264 or VC-1 EVOs. -->

## Issues found in the first version, all fixed in the patch now

1. **Major: breaks the build with MSVC.** `hddvddec_stream_add` used the
   GNU `a ?: b` operator. MSVC, which FFmpeg supports (`doc/platform.texi`),
   does not accept it. In FFmpeg it appears only in Linux-only V4L2 code.
   Fixed: the result is kept in a variable and tested explicitly. The file
   now compiles under `clang -std=c17 -Wpedantic` with no warnings from it.
2. **Major class: NULL pointer dereference after a failed reopen.**
   `hddvddec_subdemux_open` closes the old sub-demuxer first. If opening
   the new one then failed (an allocation failure, or `avformat_open_input`
   failing at a seek or a join), `mpeg_ctx` was left NULL. The caller's next
   `av_read_frame` then reached `av_read_frame(NULL, ...)` in
   `hddvddec_read_packet`. This only happens after a failure. Fixed:
   `read_packet` returns the read error, or `AVERROR(EIO)`, when there is no
   sub-demuxer.
3. **Moderate: Advanced Content timestamps late by up to one EVOBU.**
   libhddvd documents that a clip's extent "runs from the EVOBU holding the
   clip's first frame". That EVOBU can start before the clip's
   `clipTimeBegin`. The demuxer mapped the first video PTS at the extent's
   start onto the clip's title start, which shifted every frame of such a
   clip late.
   - Reproduced on a synthetic title whose clip starts 1.5 s into its EVO,
     in the middle of a time map entry: frames ran 0.300 s to 2.769 s,
     past the title's 2.502 s end.
   - Fixed: for Advanced Content, the title's time maps give the shift
     (`hddvd_title_map_locate`, whose `pts` is the EVO time of the title
     time). They are used when they agree to within a second with the first
     video PTS at the extent's start. Otherwise that PTS is used, as before.
   - The same title now plays 0.000 s to 2.469 s, its 75 frames. A disc
     whose VTI start times are 5 s off falls back as intended.
4. **Minor: video codec not found in minimal builds.** The MPEG-PS
   demuxer finds a video stream's codec by probing it with the `mpegvideo`
   and `h264` raw demuxers (`fmt_id_type` in `demux.c`). Unlike `dvdvideo`,
   this demuxer does not set the codec itself. With
   `--disable-everything --enable-demuxer=hddvd`, video showed as
   `codec none`. Fixed: `hddvd_demuxer_select` adds `mpegvideo_demuxer` and
   `h264_demuxer`. VC-1 needs nothing, because `mpeg.c` sets it from the
   0xfd55 stream id.

## Conditional concerns (not changed, each rests on an unverified assumption)

- **H.264 key frames.** After a start, restart or seek, packets are dropped
  until a video key frame. If HD DVD H.264 EVOs carry neither IDR pictures
  nor recovery-point SEI at EVOBU starts, the H.264 parser would mark no key
  frames. Each start would then wait for 1024 packets before giving up.
  Unverified: no H.264 EVO was available.
- **Secondary audio languages.** The audio stream number is the substream
  id `& 7`, as in the VLC module. If secondary audio uses substream ids 8
  higher than the primary ones (for example 0xc8 to 0xcf), it would get the
  primary track's language. Unverified on a disc.
- **33-bit PTS wrap.** The extent PTS read from the PES header is not
  unwrapped. The sub-demuxer's packet timestamps may be. This only matters
  for an EVO whose timestamps wrap; none was seen.
- **No FATE test.** FFmpeg's project facts ask for one for each new format
  "when possible". As with `dvdvideo` and the `bluray` protocol, it would
  need libhddvd at test time and a sample directory in fate-suite. The
  synthetic discs from `make_discs.py` are 0.6 to 1.2 MB. A test
  sample has to be sent to samples-request at ffmpeg dot org.

## Checks run on the patch as it now stands

- FFmpeg's default configuration with `--enable-libhddvd` builds, with no
  warnings from `hddvddec.c`.
- A minimal build with `--enable-demuxer=hddvd` alone pulls in `mpegps`,
  `mpegvideo` and `h264`.
- `make fate-source` passes.
- `tools/patcheck` only hints at non-doxygen member comments, the style
  `dvdvideodec.c` uses too.
- `run_checks.sh` matches `checks.expected` on the normal and the ASan +
  UBSan build, with no sanitizer reports. It covers:
  - whole titles, the mid-EVOBU clip and the VTI fallback;
  - chapter ranges and seeks;
  - remuxing to Matroska;
  - the scrambled disc;
  - refused inputs.
- The patch applies to FFmpeg master (76fa189) with `git am`.

## Classification

Had the first version been posted, a Fairy-style verdict would have been
**major_issues**. Fairy's major list includes "Breaks the build in any
supported configuration" (item 1) and "NULL pointer dereference" (item 2).
With the four fixes, what remains are the conditional concerns above and
the missing FATE test, which is a suggestion. That would fit
**minor_issues_approve**: "only minor or pre-existing issues, non-blocking
issues or suggestions". That is this review's reading, not the bot's
verdict. The bot runs a different model and may find other issues. It
reviews pull requests on code.ffmpeg.org. The reference launcher only
reviews one on its own initiative after 56 days without discussion activity
(`--min-age-days 56`). Mentioning the bot in a comment or requesting it as
a reviewer skips that wait.
