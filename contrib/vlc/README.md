# HD DVD module for VLC 4 (libhddvd)

Patches against VLC master (99f4e71), for a merge request on
code.videolan.org. They are kept here until they are submitted; the module
itself belongs in VLC's tree.

1. `0001-contrib-add-libhddvd.patch` – contrib package for libhddvd
   0.6.0, the first release under the LGPL (2.1 or later, as libbluray),
   so it builds in VLC's LGPL builds too, with no GPL gate. Its SHA512SUMS
   file needs the v0.6.0 tarball: the tag is to be made once 0.6.0 is on
   libhddvd's main, and the repository is private, so its tarball cannot
   be downloaded yet. Once both hold, `fill_sha512.sh VLC_TREE OUT_DIR` downloads its tarball, adds
   `contrib/src/hddvd/SHA512SUMS` to this commit, and re-exports the
   three patches.
2. `0002-demux-ps-optionally-give-streams-their-stream-id-as-.patch` – a
   private `ps-stream-ids` option, so a module chaining the PS demuxer can
   tell its streams apart. Without it the PS demuxer behaves as before.
3. `0003-access-add-HD-DVD-module-using-libhddvd.patch` – the module,
   under the LGPLv2.1+ as VLC's other access modules are. It needs
   libhddvd 0.6.0 (it uses the Standard Content guesses,
   `hddvd_std_nav_set_options` and the menu-cell functions). The
   experimental `--hddvd-guess` option (off by default) turns them on: menu
   and first-play cells play, a still holds (255 until a button is
   activated, 1 to 254 seconds otherwise), VLC's navigation keys follow the
   buttons' up/down/left/right and activate them, and the mouse selects
   the button under it and activates it on a press. Button highlights are
   not drawn (the subpicture's highlight colours are not applied). A menu
   cell reports no length and cannot be sought in. These layouts are DVD-Video's,
   unconfirmed on any HD DVD disc.

## Headless checks (not part of the patches)

Build VLC with avcodec, swscale and png and install it to `$PREFIX`; build
libhddvd with QuickJS and install it.

- `check_standard.c` – the Standard Content fixture in libhddvd's
  `tests/fixtures/standard`: navigation, title and chapter choices, seeks
  through the navigator, `--no-hddvd-menu`, opening the directory, an
  encrypted copy, a directory that is not a disc, and `--hddvd-guess` on
  `tests/fixtures/guesses` (first play, the still title menu, keys moving
  between and activating buttons across two menu PGCs, the angle block
  and PGC links; its title menu's scrambled packet ending in the
  encryption dialog; without the option, the menu refused). Build with
  `-DHDDVD_FIXTURE_DIR='"<libhddvd>/tests/fixtures"'`.
- `make_ac_disc.py DIR MEDIA [scramble]` builds an Advanced Content disc
  with a menu from MEDIA/one.vob, two.vob (MPEG-2 program streams, about
  8 Mbit/s so that pausing takes effect within a second) and a.png, b.png.
- `make_std_menu_disc.py FIXTURE STILL OUT` copies libhddvd's
  `tests/fixtures/guesses` to OUT with a decodable title menu: STILL is an
  MPEG-2 program stream in 2048-byte packs of a 1920x1080 still, e.g.
  `ffmpeg -f lavfi -i color=c=0x203040:s=1920x1080:r=30000/1001:d=0.2
  -c:v mpeg2video -g 1 -q:v 20 -an -f vob -packetsize 2048 still.vob`,
  and first play's cell gets a 2-second still.
- `check_menu.c DISC SCRAMBLED_DISC [overlay [STD_MENU_DISC]]` – the menu runs, keys move
  its focus and its jump changes the title; with `overlay` (a VLC that
  decodes the video) the overlay is shown and the mouse clicks its buttons,
  which pause and play, choose the audio track by number and language,
  hide subtitles, reorder and (refused) inactivate the application, and
  jump. The jump into the scrambled disc ends in the encryption dialog.
  With STD_MENU_DISC (from `make_std_menu_disc.py`), `--hddvd-guess`:
  first play's still holds for its 2 seconds, moving the mouse over a
  Standard Content menu button selects it, and a click plays the title.
- `testwin.c` – a window plugin for the `overlay` run: it reports mouse
  events from the check through VLC's own video output. Build it as
  `libtestwin_plugin.so` against VLC's plugin headers (linking libvlccore)
  into a directory named by `VLC_PLUGIN_PATH`, for that run only.

Link the checks with `-rdynamic` (testwin finds `check_window` in the
process).
