/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Lists a disc's playable titles: the Standard Content titles (the VMG's
   title table) and the Advanced Content titles (the playlist's), each with
   its chapters and the EVO extents it plays. --read also reads every title
   through and says whether its data is clear program stream, encrypted or
   damaged. --navigate follows the Standard Content disc's own commands from
   first play for up to COUNT cells. --guess turns on the navigator's
   guesses, which read DVD-Video's layout where HD DVD's is unknown (menu
   cells and their buttons, angle blocks, PGC links): a menu cell is listed
   with its buttons, and its selected button is pressed, as a test of the
   guesses on a real disc.

   --streams sets three accounts of a disc's audio and subpicture streams
   side by side, so that a disc can show how they correspond: for each
   playlist title its tracks (kind, track number, streamNumber, language,
   codec), and for each primary clip the audio streams the VTI's attribute
   record lists and the streams the clip's EVO carries (Standard Content:
   each title set's audio and subpicture attributes and its title EVO).
   EVO streams are counted from packet headers alone, which AACS leaves
   clear, so encrypted discs can be probed; packets marked scrambled are
   counted apart. Each EVO is scanned from its start for --scan MIB
   (default 256; 0 for the whole file), so a subpicture stream that first
   appears later is missed. Nothing is decrypted or written. */

static double seconds(hddvd_time time)
{
    return (double)time.ticks / 90000.0;
}

static void report(hddvd_ctx *ctx, const char *what, int rc)
{
    hddvd_error_detail detail;
    printf("%s: %s", what, hddvd_strerror(rc));
    if (hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == rc && detail.message[0])
        printf(" (%s: %s)", detail.resource, detail.message);
    printf("\n");
}

/* Returns the number of titles whose data could not be read through. */
static int list(hddvd_ctx *ctx, hddvd_titles *titles, int read)
{
    static unsigned char buffer[64 * 2048];
    size_t title, i;
    int unread = 0;
    for (title = 0; title < hddvd_titles_count(titles); title++)
    {
        hddvd_title_info info;
        hddvd_titles_get_info(titles, title, &info);
        printf("title %zu duration=%.3fs sectors=%llu chapters=%zu extents=%zu", title + 1,
               seconds(info.duration), (unsigned long long)(info.size / 2048u), info.chapter_count,
               info.extent_count);
        if (info.title_set)
            printf(" title-set=%u/%u angles=%u", info.title_set, info.title_set_title,
                   info.angle_count);
        printf("\n");
        for (i = 0; i < info.chapter_count; i++)
        {
            hddvd_title_chapter chapter;
            hddvd_titles_get_chapter(titles, title, i, &chapter);
            printf("  chapter %zu start=%.3fs sector=%llu\n", i + 1, seconds(chapter.start),
                   (unsigned long long)(chapter.offset / 2048u));
        }
        for (i = 0; i < info.extent_count; i++)
        {
            hddvd_title_extent extent;
            hddvd_titles_get_extent(titles, title, i, &extent);
            printf("  extent %zu %s sectors=%lu+%lu start=%.3fs duration=%.3fs\n", i + 1,
                   extent.evo_uri, (unsigned long)extent.first_sector,
                   (unsigned long)extent.sector_count, seconds(extent.start),
                   seconds(extent.duration));
        }
        if (read)
        {
            uint64_t offset = 0;
            size_t got = 0;
            int rc = HDDVD_OK;
            while (offset < info.size &&
                   (rc = hddvd_titles_read(titles, title, offset, buffer, sizeof(buffer), &got)) ==
                       HDDVD_OK &&
                   got)
                offset += got;
            if (rc)
            {
                char what[64];
                offset += got;
                snprintf(what, sizeof(what), "  read stopped at sector %llu",
                         (unsigned long long)(offset / 2048u));
                report(ctx, what, rc);
                unread++;
            }
            else
                printf("  read ok\n");
        }
    }
    return unread;
}

#define URI_MAX 1024

/* Streams found in an EVO's packet headers: (stream id, substream id),
   with the substream the first payload byte of private stream 1 (0xbd)
   and 0 otherwise. */
typedef struct evo_stream
{
    unsigned stream_id;
    unsigned substream_id;
    unsigned long long packets;
    unsigned long long scrambled;
} evo_stream;

static const char *stream_codec(unsigned sid, unsigned sub)
{
    if (sid == 0xe0)
        return "MPEG-2 video";
    if (sid == 0xe2)
        return "H.264/AVC";
    if (sid == 0xfd)
        return "VC-1";
    if (sid >= 0xc0 && sid <= 0xdf)
        return "MPEG audio";
    if (sid != 0xbd)
        return "unknown";
    if (sub >= 0x20 && sub <= 0x3f)
        return "subpicture";
    if (sub >= 0x88 && sub <= 0x9f)
        return "DTS-HD";
    if (sub >= 0xa0 && sub <= 0xaf)
        return "LPCM";
    if (sub >= 0xb0 && sub <= 0xbf)
        return "MLP/TrueHD";
    if (sub >= 0xc0 && sub <= 0xcf)
        return "AC-3/E-AC-3";
    return "unknown";
}

static void count_stream(evo_stream *streams, size_t *count, unsigned sid, unsigned sub,
                         int scrambled)
{
    size_t i;
    for (i = 0; i < *count; i++)
        if (streams[i].stream_id == sid && streams[i].substream_id == sub)
            break;
    if (i == *count)
    {
        if (*count == 256)
            return;
        memset(&streams[i], 0, sizeof(streams[i]));
        streams[i].stream_id = sid;
        streams[i].substream_id = sub;
        (*count)++;
    }
    streams[i].packets++;
    if (scrambled)
        streams[i].scrambled++;
}

/* The PES packets of one 2048-byte pack. Returns 0 when it is not a pack. */
static int scan_pack(const unsigned char *b, evo_stream *streams, size_t *count)
{
    size_t at;
    if (b[0] || b[1] || b[2] != 1 || b[3] != 0xba)
        return 0;
    at = 14u + (b[13] & 7u);
    while (at + 6 <= 2048 && !b[at] && !b[at + 1] && b[at + 2] == 1)
    {
        unsigned sid = b[at + 3];
        size_t length = (size_t)b[at + 4] << 8 | b[at + 5];
        if (sid != 0xbb && sid != 0xbe && sid != 0xbf && length >= 3 && at + 9 <= 2048)
        {
            size_t payload = at + 9u + b[at + 8];
            unsigned sub = 0;
            if (sid == 0xbd && payload < at + 6 + length && payload < 2048)
                sub = b[payload];
            count_stream(streams, count, sid, sub, (b[at + 6] >> 4) & 3);
        }
        at += 6 + length;
    }
    return 1;
}

/* EVO URIs already scanned, so that an EVO several titles play is listed
   once. */
static char scanned[64][URI_MAX];
static size_t scanned_count;

static int scan_evo(hddvd_ctx *ctx, const char *uri, unsigned long scan_mib)
{
    static unsigned char buffer[64 * 2048];
    static evo_stream streams[256];
    hddvd_stream *stream = NULL;
    uint64_t offset = 0, limit = (uint64_t)scan_mib * 1024u * 1024u;
    size_t count = 0, i, got = 0;
    unsigned long long packs = 0, other = 0;
    int rc;
    for (i = 0; i < scanned_count; i++)
        if (!strcmp(scanned[i], uri))
        {
            printf("    evo %s listed above\n", uri);
            return 0;
        }
    if (scanned_count < 64)
        snprintf(scanned[scanned_count++], URI_MAX, "%s", uri);
    rc = hddvd_open_resource(ctx, uri, &stream);
    if (rc)
    {
        char what[URI_MAX + 16];
        snprintf(what, sizeof(what), "    evo %s", uri);
        report(ctx, what, rc);
        return 1;
    }
    while ((!limit || offset < limit) &&
           (rc = hddvd_stream_read_at(stream, offset, buffer, sizeof(buffer), &got)) == HDDVD_OK &&
           got >= 2048)
    {
        for (i = 0; i + 2048 <= got; i += 2048)
        {
            if (scan_pack(buffer + i, streams, &count))
                packs++;
            else
                other++;
        }
        offset += got - got % 2048;
    }
    hddvd_stream_close(stream);
    printf("    evo %s scanned=%llu sectors packs=%llu other=%llu%s\n", uri,
           (unsigned long long)(offset / 2048u), packs, other,
           limit && offset >= limit ? " (stopped at --scan)" : "");
    for (i = 0; i < count; i++)
    {
        if (streams[i].stream_id == 0xbd)
            printf("      stream 0xbd/0x%02x", streams[i].substream_id);
        else
            printf("      stream 0x%02x", streams[i].stream_id);
        printf(" %s packets=%llu scrambled=%llu\n",
               stream_codec(streams[i].stream_id, streams[i].substream_id), streams[i].packets,
               streams[i].scrambled);
    }
    return rc ? 1 : 0;
}

static void print_attributes(const char *what, size_t index, const hddvd_stream_attributes *a,
                             size_t size)
{
    size_t i;
    printf("    %s %zu language=%s raw=", what, index + 1, a->language[0] ? a->language : "-");
    for (i = 0; i < size; i++)
        printf("%02x", a->raw[i]);
    printf("\n");
}

static const char *media_kind(hddvd_xpl_media_kind kind)
{
    return kind == HDDVD_XPL_MEDIA_AUDIO        ? "audio"
           : kind == HDDVD_XPL_MEDIA_SUBPICTURE ? "subpicture"
                                                : "video";
}

/* The codec the playlist's media attribute item names, or NULL. */
static const char *track_codec(const hddvd_xpl *xpl, const hddvd_xpl_track *track)
{
    size_t i;
    if (!track->has_media_attribute)
        return NULL;
    for (i = 0; i < hddvd_xpl_media_attribute_count(xpl); i++)
    {
        hddvd_xpl_media_attribute m;
        if (hddvd_xpl_get_media_attribute(xpl, i, &m) == HDDVD_OK && m.kind == track->kind &&
            m.index == track->media_attribute)
            return m.codec;
    }
    return NULL;
}

/* A primary clip: its time map names the VTI and EVOB, whose attribute
   record lists the audio streams, and whose EVO is scanned. */
static int clip_streams(hddvd_ctx *ctx, const char *src, unsigned long scan_mib)
{
    char dir[URI_MAX], uri[URI_MAX + 64];
    const char *slash = strrchr(src, '/');
    hddvd_stream *stream = NULL;
    hddvd_tmap *tmap = NULL;
    hddvd_vti *vti = NULL;
    hddvd_map_header header;
    hddvd_vti_evob evob;
    size_t at, i;
    int rc;
    if (!slash || (size_t)(slash - src) + 1 >= sizeof(dir))
    {
        printf("  clip %s: no directory\n", src);
        return 1;
    }
    memcpy(dir, src, (size_t)(slash - src) + 1);
    dir[slash - src + 1] = '\0';
    rc = hddvd_open_resource(ctx, src, &stream);
    if (!rc)
        rc = hddvd_tmap_open(stream, &tmap);
    hddvd_stream_close(stream);
    stream = NULL;
    if (!rc)
        rc = hddvd_tmap_get_header(tmap, &header);
    if (!rc)
    {
        snprintf(uri, sizeof(uri), "%s%s", dir, header.vti_name);
        rc = hddvd_open_resource(ctx, uri, &stream);
    }
    if (!rc)
        rc = hddvd_vti_open(stream, &vti);
    hddvd_stream_close(stream);
    if (!rc)
        rc = hddvd_vti_find_evob(vti, hddvd_tmap_evob_number(tmap), &at);
    if (!rc)
        rc = hddvd_vti_get_evob(vti, at, &evob);
    if (rc)
    {
        char what[URI_MAX + 16];
        snprintf(what, sizeof(what), "  clip %s", src);
        report(ctx, what, rc);
        hddvd_vti_close(vti);
        hddvd_tmap_close(tmap);
        return 1;
    }
    printf("  clip %s vti=%s evob=%u attributes=%u\n", src, header.vti_name, evob.number,
           evob.attribute_number);
    if (evob.attribute_number)
        for (i = 0; i < hddvd_vti_audio_count(vti, evob.attribute_number - 1u); i++)
        {
            hddvd_stream_attributes a;
            if (hddvd_vti_get_audio_attributes(vti, evob.attribute_number - 1u, i, &a) == HDDVD_OK)
                print_attributes("vti audio", i, &a, 8);
        }
    snprintf(uri, sizeof(uri), "%s%s", dir, evob.file_name);
    hddvd_vti_close(vti);
    hddvd_tmap_close(tmap);
    return scan_evo(ctx, uri, scan_mib);
}

static int playlist_streams(hddvd_ctx *ctx, const hddvd_xpl *xpl, unsigned long scan_mib)
{
    size_t title, i;
    int failed = 0;
    for (title = 0; title < hddvd_xpl_title_count(xpl); title++)
    {
        hddvd_xpl_title_info info;
        hddvd_xpl_get_title(xpl, title, &info);
        printf("streams title %zu id=%s\n", title + 1, info.id ? info.id : "-");
        for (i = 0; i < hddvd_xpl_track_count(xpl, title); i++)
        {
            hddvd_xpl_track track;
            hddvd_xpl_track_details details;
            const char *codec;
            if (hddvd_xpl_get_track(xpl, title, i, &track) != HDDVD_OK ||
                track.kind == HDDVD_XPL_MEDIA_VIDEO)
                continue;
            memset(&details, 0, sizeof(details));
            hddvd_xpl_get_track_details(xpl, title, i, &details);
            codec = track_codec(xpl, &track);
            printf("  track %s %lu", media_kind(track.kind), (unsigned long)track.track);
            if (track.has_stream_number)
                printf(" streamNumber=%lu", (unsigned long)track.stream_number);
            printf(" language=%s", track.language ? track.language : "-");
            if (codec)
                printf(" codec=%s", codec);
            printf("%s\n", details.secondary ? " secondary" : "");
        }
        for (i = 0; i < hddvd_xpl_clip_count(xpl, title); i++)
        {
            hddvd_xpl_clip clip;
            hddvd_xpl_clip_kind kind;
            if (hddvd_xpl_get_clip_kind(xpl, title, i, &kind) == HDDVD_OK &&
                kind == HDDVD_XPL_CLIP_PRIMARY_AUDIO_VIDEO &&
                hddvd_xpl_get_clip(xpl, title, i, &clip) == HDDVD_OK && clip.src)
                failed += clip_streams(ctx, clip.src, scan_mib);
        }
    }
    return failed;
}

/* Standard Content: each title set's stream attributes and title EVO. */
static int title_set_streams(hddvd_ctx *ctx, hddvd_titles *titles, unsigned long scan_mib)
{
    unsigned set, last = 0;
    size_t title, i;
    int failed = 0;
    for (title = 0; title < hddvd_titles_count(titles); title++)
    {
        hddvd_title_info info;
        if (hddvd_titles_get_info(titles, title, &info) == HDDVD_OK && info.title_set > last)
            last = info.title_set;
    }
    for (set = 1; set <= last; set++)
    {
        char uri[64];
        hddvd_stream *stream = NULL;
        hddvd_vts *vts = NULL;
        int rc;
        snprintf(uri, sizeof(uri), "file:///dvddisc/HVDVD_TS/HV%03uI01.IFO", set);
        printf("streams title-set %u\n", set);
        rc = hddvd_open_resource(ctx, uri, &stream);
        if (!rc)
            rc = hddvd_vts_open(stream, &vts);
        hddvd_stream_close(stream);
        if (rc)
        {
            report(ctx, "    title set", rc);
            failed++;
            continue;
        }
        for (i = 0; i < hddvd_vts_stream_count(vts, HDDVD_STREAM_AUDIO); i++)
        {
            hddvd_stream_attributes a;
            if (hddvd_vts_get_stream_attributes(vts, HDDVD_STREAM_AUDIO, i, &a) == HDDVD_OK)
                print_attributes("ifo audio", i, &a, 8);
        }
        for (i = 0; i < hddvd_vts_stream_count(vts, HDDVD_STREAM_SUBPICTURE); i++)
        {
            hddvd_stream_attributes a;
            if (hddvd_vts_get_stream_attributes(vts, HDDVD_STREAM_SUBPICTURE, i, &a) == HDDVD_OK)
                print_attributes("ifo subpicture", i, &a, 6);
        }
        hddvd_vts_close(vts);
        snprintf(uri, sizeof(uri), "file:///dvddisc/HVDVD_TS/HV%03uT01.EVO", set);
        failed += scan_evo(ctx, uri, scan_mib);
    }
    return failed;
}

/* A menu step under the guesses: the cell, its buttons, then the selected
   button pressed (or, without buttons, the next cell). */
static int menu(hddvd_std_nav *nav, hddvd_std_nav_step *step)
{
    hddvd_std_nav_menu_cell cell;
    hddvd_std_nav_button b;
    size_t i, count = hddvd_std_nav_button_count(nav);
    hddvd_std_nav_get_menu_cell(nav, &cell);
    printf("navigate menu title-set=%u pgc=%u cell=%u evo=%s sectors=%lu+%lu still=%u "
           "buttons=%zu\n",
           step->title_set, step->pgc, step->cell, cell.evo_uri, (unsigned long)cell.first_sector,
           (unsigned long)cell.sector_count, cell.still, count);
    for (i = 0; i < count && hddvd_std_nav_get_button(nav, i, &b) == HDDVD_OK; i++)
        printf("  button %zu at %lu,%lu size %lux%lu up=%u down=%u left=%u right=%u%s\n", i + 1,
               (unsigned long)b.x, (unsigned long)b.y, (unsigned long)b.width,
               (unsigned long)b.height, b.up, b.down, b.left, b.right,
               b.auto_action ? " auto" : "");
    if (!count)
        return hddvd_std_nav_next(nav, step);
    printf("  press button %u\n", hddvd_std_nav_selected_button(nav));
    return hddvd_std_nav_activate_button(nav, step);
}

static int navigate(hddvd_ctx *ctx, unsigned long cells, int guess)
{
    hddvd_std_nav *nav = NULL;
    hddvd_std_nav_step step;
    unsigned long i;
    int rc = hddvd_std_nav_open(ctx, &nav);
    if (!rc && guess)
        rc = hddvd_std_nav_set_options(nav, HDDVD_STD_NAV_GUESS_PGC_LINKS |
                                                HDDVD_STD_NAV_GUESS_ANGLES |
                                                HDDVD_STD_NAV_GUESS_MENU_CELLS);
    if (!rc)
        rc = hddvd_std_nav_start(nav, &step);
    for (i = 0; !rc && i < cells; i++)
    {
        if (step.kind == HDDVD_STD_NAV_MENU)
        {
            rc = menu(nav, &step);
            continue;
        }
        if (step.kind != HDDVD_STD_NAV_PLAY)
        {
            printf("navigate %s: %s\n", step.kind == HDDVD_STD_NAV_STOP ? "stop" : "unsupported",
                   step.reason);
            break;
        }
        printf("navigate play title=%zu chapter=%zu sectors=%llu+%llu pgc=%u cell=%u\n",
               step.title + 1, step.chapter + 1, (unsigned long long)(step.offset / 2048u),
               (unsigned long long)(step.size / 2048u), step.pgc, step.cell);
        rc = hddvd_std_nav_next(nav, &step);
    }
    if (rc)
        report(ctx, "navigate", rc);
    hddvd_std_nav_close(nav);
    return rc ? 1 : 0;
}

int main(int argc, char **argv)
{
    hddvd_ctx *ctx;
    hddvd_titles *titles = NULL;
    hddvd_xpl *xpl = NULL;
    hddvd_error_detail detail;
    const char *root = NULL;
    unsigned long cells = 0, scan_mib = 256;
    int read = 0, guess = 0, streams = 0, a, rc, found = 0, failed = 0;
    for (a = 1; a < argc; a++)
    {
        if (!strcmp(argv[a], "--read"))
            read = 1;
        else if (!strcmp(argv[a], "--guess"))
            guess = 1;
        else if (!strcmp(argv[a], "--streams"))
            streams = 1;
        else if (!strcmp(argv[a], "--scan") && a + 1 < argc)
            scan_mib = strtoul(argv[++a], NULL, 10);
        else if (!strcmp(argv[a], "--navigate") && a + 1 < argc)
            cells = strtoul(argv[++a], NULL, 10);
        else if (!root && argv[a][0] != '-')
            root = argv[a];
        else
            root = NULL, a = argc;
    }
    if (!root)
    {
        fprintf(stderr,
                "usage: %s [--read] [--navigate COUNT [--guess]] [--streams [--scan MIB]] "
                "DISC_ROOT\n",
                argv[0]);
        return 2;
    }
    ctx = hddvd_open(root);
    if (!ctx)
    {
        fprintf(stderr, "%s: cannot open %s\n", argv[0], root);
        return 1;
    }

    rc = hddvd_titles_open(ctx, &titles);
    if (rc == HDDVD_OK)
    {
        printf("standard-content titles=%zu\n", hddvd_titles_count(titles));
        failed += list(ctx, titles, read);
        if (streams)
            failed += title_set_streams(ctx, titles, scan_mib);
        hddvd_titles_close(titles);
        found = 1;
        if (cells)
            failed += navigate(ctx, cells, guess);
    }
    /* Not finding the VMG itself only means no Standard Content. */
    else if (rc != HDDVD_ENOTFOUND || hddvd_get_last_error(ctx, &detail) != HDDVD_OK ||
             !strstr(detail.resource, "HV000I01.IFO"))
    {
        report(ctx, "standard-content", rc);
        failed++;
    }

    titles = NULL;
    rc = hddvd_open_playlist(ctx, &xpl);
    if (rc == HDDVD_OK)
        rc = hddvd_titles_open_playlist(ctx, xpl, &titles);
    if (rc == HDDVD_OK)
    {
        printf("advanced-content titles=%zu\n", hddvd_titles_count(titles));
        failed += list(ctx, titles, read);
        if (streams)
            failed += playlist_streams(ctx, xpl, scan_mib);
        hddvd_titles_close(titles);
        found = 1;
    }
    else if (xpl || hddvd_has_advanced_content(ctx))
    {
        report(ctx, "advanced-content", rc);
        failed++;
    }
    hddvd_xpl_close(xpl);
    hddvd_close(ctx);
    if (!found && !failed)
    {
        printf("no titles\n");
        return 1;
    }
    printf("summary %s\n", failed ? "FAILED" : "ok");
    return failed ? 1 : 0;
}
