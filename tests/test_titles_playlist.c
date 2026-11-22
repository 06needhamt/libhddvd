/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0755)
#define RMDIR(p) rmdir(p)
#endif

/* An original Advanced Content disc, built here: ONE.EVO (EVOB 1: EVOBUs of
   10 and 20 sectors, 10 frames each) and TWO.EVO (EVOB 2: two EVOBUs of 30
   sectors and 20 frames), their time maps and VTI, and playlists. Frames
   are at 60000/1001, 3003/2 ticks each. */
#define ROOT "titles-playlist-disc"
#define TS ROOT "/HVDVD_TS/"
#define ADV ROOT "/ADV_OBJ/"
#define SECTOR 2048u
#define FRAMES(n) ((uint64_t)(n) * 3003u / 2u)

static void put32(unsigned char *b, uint32_t v)
{
    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);
    b[3] = (unsigned char)v;
}

static void write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(data, 1, size, f) == size);
    fclose(f);
}

/* Each entry is (reference, units, sectors). */
static void write_map(const char *name, unsigned evob, const uint32_t (*entries)[3], size_t count)
{
    unsigned char b[2048] = {0};
    char path[64];
    size_t i;
    memcpy(b, "HDDVD_TMAP00", 12);
    memcpy(b + 0x72, "HVA00001.VTI", 12);
    put32(b + 0x180, 0x1a0);
    b[0x185] = (unsigned char)evob;
    b[0x187] = (unsigned char)count;
    for (i = 0; i < count; i++)
        put32(b + 0x1a0 + 4 * i, entries[i][0] << 21 | entries[i][1] << 11 | entries[i][2]);
    snprintf(path, sizeof(path), TS "%s", name);
    write_file(path, b, sizeof(b));
}

static void write_vti(void)
{
    static const struct
    {
        const char *name;
        uint32_t start, end, sectors;
    } evobs[] = {{"ONE.EVO", 1000, 1000 + 30030, 30}, {"TWO.EVO", 2000, 2000 + 60060, 60}};
    unsigned char b[4096] = {0}, *t = b + 2048;
    size_t i;
    memcpy(b, "ADVANCED-VTS", 12);
    put32(b + 0x1c, 1);
    put32(b + 0xbc, 1);
    t[3] = 2;
    put32(t + 4, 16 + 2 * 0x140 - 1);
    for (i = 0; i < 2; i++)
    {
        unsigned char *r = t + 16 + 0x140 * i;
        put32(t + 8 + 4 * i, (uint32_t)(r - t));
        memcpy(r + 2, evobs[i].name, strlen(evobs[i].name));
        r[0x109] = (unsigned char)(i + 1);
        put32(r + 0x10a, evobs[i].start);
        put32(r + 0x10e, evobs[i].end);
        put32(r + 0x112, evobs[i].sectors);
    }
    write_file(TS "HVA00001.VTI", b, sizeof(b));
}

/* A pack holding one padding packet; bytes 100 and 101 name the file and
   the sector. `scrambled` makes sector 5 a video packet marked scrambled. */
static void write_evo(const char *name, unsigned id, unsigned sectors, int scrambled)
{
    static const unsigned char pack[] = {0, 0,    1,    0xba, 0x44, 0, 4, 0,    4,    1,
                                         1, 0x89, 0xc3, 0xf8, 0,    0, 1, 0xbe, 0x07, 0xec};
    unsigned char *bytes = malloc((size_t)sectors * SECTOR);
    char path[64];
    unsigned i;
    assert(bytes);
    memset(bytes, 0xff, (size_t)sectors * SECTOR);
    for (i = 0; i < sectors; i++)
    {
        unsigned char *b = bytes + (size_t)i * SECTOR;
        memcpy(b, pack, sizeof(pack));
        b[100] = (unsigned char)id;
        b[101] = (unsigned char)i;
        if (scrambled && i == 5)
        {
            b[17] = 0xe0;
            b[20] = 0x90;
        }
    }
    snprintf(path, sizeof(path), TS "%s", name);
    write_file(path, bytes, (size_t)sectors * SECTOR);
    free(bytes);
}

#define HEAD "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
#define CLIP "<PrimaryAudioVideoClip src='file:///dvddisc/HVDVD_TS/"
#define CHAPTER(t) "<Chapter titleTimeBegin='00:00:00:" t "'/>"

static const char playlist[] =
    HEAD "<TitleSet timeBase='60fps'>"
         /* 0: all of ONE.EVO, then TWO.EVO from its frame 10, then 10 frames
            with no clip. The clips are listed out of order. */
         "<Title id='main' titleDuration='00:00:01:00'>" CLIP
         "TWO.MAP' titleTimeBegin='00:00:00:20' clipTimeBegin='00:00:00:10' "
         "titleTimeEnd='00:00:00:50'/>" CLIP
         "ONE.MAP' titleTimeBegin='00:00:00:00' titleTimeEnd='00:00:00:20'/>"
         "<ChapterList>" CHAPTER("00") CHAPTER("10") CHAPTER("20") CHAPTER("45")
             CHAPTER("55") "</ChapterList></Title>"
                           /* 1: the second EVOBU of TWO.EVO only. */
                           "<Title id='tail' titleDuration='00:00:00:10'>" CLIP
                           "TWO.MAP' titleTimeBegin='00:00:00:00' clipTimeBegin='00:00:00:20' "
                           "titleTimeEnd='00:00:00:10'/></Title>"
                           /* 2: no primary clips. */
                           "<Title id='menu' titleDuration='00:00:01:00'></Title>"
                           "</TitleSet></Playlist>";

static const char overlapping[] =
    HEAD "<TitleSet timeBase='60fps'><Title id='t' titleDuration='00:00:01:00'>" CLIP
         "ONE.MAP' titleTimeBegin='00:00:00:00' titleTimeEnd='00:00:00:20'/>" CLIP
         "TWO.MAP' titleTimeBegin='00:00:00:19' titleTimeEnd='00:00:00:40'/>"
         "</Title></TitleSet></Playlist>";

static const char one_title[] =
    HEAD "<TitleSet timeBase='60fps'><Title id='only' titleDuration='00:00:00:20'>" CLIP
         "ONE.MAP' titleTimeBegin='00:00:00:00' titleTimeEnd='00:00:00:20'/>"
         "</Title></TitleSet></Playlist>";

static hddvd_xpl *parse(const char *text)
{
    hddvd_xpl *xpl = NULL;
    assert(hddvd_xpl_open(text, strlen(text), &xpl) == HDDVD_OK);
    return xpl;
}

static void listing(const hddvd_titles *titles)
{
    static const uint64_t offsets[] = {0, 10, 30, 60, 90};
    static const unsigned frames[] = {0, 10, 20, 45, 55};
    hddvd_title_info info;
    hddvd_title_chapter chapter;
    hddvd_title_extent extent;
    size_t i, index;

    assert(hddvd_titles_count(titles) == 3);
    assert(hddvd_titles_get_info(titles, 0, &info) == HDDVD_OK);
    assert(info.extent_count == 2 && info.chapter_count == 5 && info.size == 90u * SECTOR);
    assert(info.duration.ticks == FRAMES(60));
    assert(info.title_set == 0 && info.title_set_title == 0 && info.angle_count == 0);
    assert(hddvd_titles_get_extent(titles, 0, 0, &extent) == HDDVD_OK);
    assert(!strcmp(extent.evo_uri, "file:///dvddisc/HVDVD_TS/ONE.EVO"));
    assert(extent.first_sector == 0 && extent.sector_count == 30 && extent.offset == 0 &&
           extent.start.ticks == 0 && extent.duration.ticks == FRAMES(20));
    assert(hddvd_titles_get_extent(titles, 0, 1, &extent) == HDDVD_OK);
    assert(!strcmp(extent.evo_uri, "file:///dvddisc/HVDVD_TS/TWO.EVO"));
    /* Frame 10 of TWO.EVO is inside its first EVOBU, which plays whole. */
    assert(extent.first_sector == 0 && extent.sector_count == 60 && extent.offset == 30u * SECTOR &&
           extent.start.ticks == FRAMES(20) && extent.duration.ticks == FRAMES(30));
    /* Chapters: the EVOBU playing their first frame; the last is after
       every clip. */
    for (i = 0; i < 5; i++)
    {
        assert(hddvd_titles_get_chapter(titles, 0, i, &chapter) == HDDVD_OK);
        assert(chapter.start.ticks == FRAMES(frames[i]));
        assert(chapter.offset == offsets[i] * SECTOR);
    }
    assert(hddvd_titles_chapter_at(titles, 0, 30u * SECTOR - 1, &index) == HDDVD_OK && index == 1);
    assert(hddvd_titles_chapter_at(titles, 0, 30u * SECTOR, &index) == HDDVD_OK && index == 2);

    assert(hddvd_titles_get_info(titles, 1, &info) == HDDVD_OK);
    assert(info.extent_count == 1 && info.chapter_count == 0 && info.size == 30u * SECTOR);
    assert(info.duration.ticks == FRAMES(10));
    assert(hddvd_titles_get_extent(titles, 1, 0, &extent) == HDDVD_OK);
    assert(extent.first_sector == 30 && extent.sector_count == 30);
    assert(hddvd_titles_chapter_at(titles, 1, 0, &index) == HDDVD_ENOTFOUND);

    assert(hddvd_titles_get_info(titles, 2, &info) == HDDVD_OK);
    assert(info.extent_count == 0 && info.size == 0 && info.duration.ticks == FRAMES(60));
}

/* Sector `sector` of the title's stream must be sector `evo_sector` of the
   EVO numbered `id`. */
static void expect(const unsigned char *stream, unsigned sector, unsigned id, unsigned evo_sector)
{
    const unsigned char *b = stream + (size_t)sector * SECTOR;
    assert(b[3] == 0xba && b[100] == id && b[101] == evo_sector);
}

static void reads(hddvd_titles *titles)
{
    unsigned char *buffer = malloc(100u * SECTOR);
    size_t got = 1;
    unsigned i;
    assert(buffer);
    assert(hddvd_titles_read(titles, 0, 0, buffer, 100u * SECTOR, &got) == HDDVD_OK);
    assert(got == 90u * SECTOR);
    for (i = 0; i < 90; i++)
        expect(buffer, i, i < 30 ? 1 : 2, i < 30 ? i : i - 30);
    /* Another title, then back: the EVO read from changes each time. */
    assert(hddvd_titles_read(titles, 1, SECTOR, buffer, 100u * SECTOR, &got) == HDDVD_OK);
    assert(got == 29u * SECTOR);
    expect(buffer, 0, 2, 31);
    assert(hddvd_titles_read(titles, 0, 29u * SECTOR, buffer, 2u * SECTOR, &got) == HDDVD_OK);
    assert(got == 2u * SECTOR);
    expect(buffer, 0, 1, 29);
    expect(buffer, 1, 2, 0);
    assert(hddvd_titles_read(titles, 2, 0, buffer, SECTOR, &got) == HDDVD_OK && got == 0);
    free(buffer);
}

static int open_text(hddvd_ctx *ctx, const char *text, hddvd_titles **titles)
{
    hddvd_xpl *xpl = parse(text);
    int rc;
    *titles = (hddvd_titles *)ctx;
    rc = hddvd_titles_open_playlist(ctx, xpl, titles);
    /* The playlist is not retained. */
    hddvd_xpl_close(xpl);
    assert(rc == HDDVD_OK || !*titles);
    return rc;
}

static void errors(hddvd_ctx *ctx)
{
    hddvd_titles *titles;
    hddvd_error_detail detail;
    hddvd_xpl *xpl = parse(playlist);
    unsigned char buffer[SECTOR];
    size_t got;

    assert(hddvd_titles_open_playlist(NULL, xpl, &titles) == HDDVD_EINVAL);
    assert(hddvd_titles_open_playlist(ctx, NULL, &titles) == HDDVD_EINVAL);
    assert(hddvd_titles_open_playlist(ctx, xpl, NULL) == HDDVD_EINVAL);
    hddvd_xpl_close(xpl);

    assert(open_text(ctx, overlapping, &titles) == HDDVD_EPARSE);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == HDDVD_EPARSE &&
           strstr(detail.message, "overlap"));

    /* A scrambled packet in ONE.EVO's sector 5. */
    write_evo("ONE.EVO", 1, 30, 1);
    assert(open_text(ctx, playlist, &titles) == HDDVD_OK);
    assert(hddvd_titles_read(titles, 0, 5u * SECTOR, buffer, SECTOR, &got) == HDDVD_EPROTECTED);
    assert(got == 0);
    assert(hddvd_titles_read(titles, 0, 4u * SECTOR, buffer, SECTOR, &got) == HDDVD_OK &&
           got == SECTOR);
    hddvd_titles_close(titles);
    write_evo("ONE.EVO", 1, 30, 0);

    /* An EVO shorter than its time map, then none. */
    write_evo("TWO.EVO", 2, 40, 0);
    assert(open_text(ctx, playlist, &titles) == HDDVD_EPARSE);
    assert(remove(TS "TWO.EVO") == 0);
    assert(open_text(ctx, playlist, &titles) == HDDVD_ENOTFOUND);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK &&
           !strcmp(detail.resource, "file:///dvddisc/HVDVD_TS/TWO.EVO"));
    /* Titles that do not use it still open. */
    assert(open_text(ctx, one_title, &titles) == HDDVD_OK);
    hddvd_titles_close(titles);
    /* A time map that is missing is the title map's failure. */
    assert(remove(TS "ONE.MAP") == 0);
    assert(open_text(ctx, one_title, &titles) == HDDVD_ENOTFOUND);
}

static void playlists(hddvd_ctx *ctx)
{
    hddvd_xpl *xpl = (hddvd_xpl *)ctx;
    hddvd_error_detail detail;

    /* No ADV_OBJ, then an empty one. */
    assert(hddvd_open_playlist(ctx, &xpl) == HDDVD_ENOTFOUND && !xpl);
    MKDIR(ROOT "/ADV_OBJ");
    assert(hddvd_open_playlist(ctx, &xpl) == HDDVD_ENOTFOUND && !xpl);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == HDDVD_ENOTFOUND);

    /* The highest number wins, whatever the case; other names are not
       playlists. */
    write_file(ADV "VPLST000.XPL", one_title, sizeof(one_title) - 1);
    write_file(ADV "vplst002.xpl", playlist, sizeof(playlist) - 1);
    write_file(ADV "VPLST00X.XPL", "x", 1);
    write_file(ADV "VPLST1234.XPL", "x", 1);
    write_file(ADV "VPLST003.TXT", "x", 1);
    assert(hddvd_open_playlist(ctx, &xpl) == HDDVD_OK && hddvd_xpl_title_count(xpl) == 3);
    hddvd_xpl_close(xpl);
    assert(remove(ADV "vplst002.xpl") == 0);
    assert(hddvd_open_playlist(ctx, &xpl) == HDDVD_OK && hddvd_xpl_title_count(xpl) == 1);
    hddvd_xpl_close(xpl);

    write_file(ADV "VPLST007.XPL", "not a playlist", 14);
    xpl = (hddvd_xpl *)ctx;
    assert(hddvd_open_playlist(ctx, &xpl) == HDDVD_EPARSE && !xpl);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK &&
           !strcmp(detail.resource, "file:///dvddisc/ADV_OBJ/VPLST007.XPL"));
    assert(hddvd_open_playlist(NULL, &xpl) == HDDVD_EINVAL);
    assert(hddvd_open_playlist(ctx, NULL) == HDDVD_EINVAL);

    remove(ADV "VPLST000.XPL");
    remove(ADV "VPLST007.XPL");
    remove(ADV "VPLST00X.XPL");
    remove(ADV "VPLST1234.XPL");
    remove(ADV "VPLST003.TXT");
    RMDIR(ROOT "/ADV_OBJ");
}

int main(void)
{
    static const uint32_t one[][3] = {{2, 40, 10}, {3, 40, 20}};
    static const uint32_t two[][3] = {{1, 80, 30}, {1, 80, 30}};
    hddvd_ctx *ctx;
    hddvd_titles *titles;

    MKDIR(ROOT);
    MKDIR(ROOT "/HVDVD_TS");
    write_map("ONE.MAP", 1, one, 2);
    write_map("TWO.MAP", 2, two, 2);
    write_vti();
    write_evo("ONE.EVO", 1, 30, 0);
    write_evo("TWO.EVO", 2, 60, 0);
    ctx = hddvd_open(ROOT);
    assert(ctx);

    playlists(ctx);
    assert(open_text(ctx, playlist, &titles) == HDDVD_OK);
    listing(titles);
    reads(titles);
    hddvd_titles_close(titles);
    errors(ctx);

    hddvd_close(ctx);
    remove(TS "ONE.EVO");
    remove(TS "TWO.MAP");
    remove(TS "HVA00001.VTI");
    RMDIR(ROOT "/HVDVD_TS");
    RMDIR(ROOT);
    return 0;
}
