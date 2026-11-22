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

#define ROOT "title-map-disc"
#define DISC_DIR ROOT "/HVDVD_TS/"

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

/* An original time map: each entry is (reference, units, sectors). */
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
    snprintf(path, sizeof(path), DISC_DIR "%s", name);
    write_file(path, b, sizeof(b));
}

/* An original VTI listing ONE.EVO (EVOB 1) and TWO.EVO (EVOB 2). */
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
    write_file(DISC_DIR "HVA00001.VTI", b, sizeof(b));
}

#define CLIP "<PrimaryAudioVideoClip src='file:///dvddisc/HVDVD_TS/"
#define TITLE(id) "<Title id='" id "' titleDuration='00:00:01:00'>"

static const char playlist[] = "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
                               "<TitleSet timeBase='60fps'>"
    /* 0: two clips, the second starting 10 frames into its EVOB, then a gap. */
    TITLE("main") CLIP "ONE.MAP' titleTimeBegin='00:00:00:00' titleTimeEnd='00:00:00:20'/>" CLIP
                       "TWO.MAP' titleTimeBegin='00:00:00:20' clipTimeBegin='00:00:00:10' "
                       "titleTimeEnd='00:00:00:50'/></Title>"
    /* 1: one frame longer than TWO.MAP allows. */
    TITLE("over") CLIP "TWO.MAP' titleTimeBegin='00:00:00:00' clipTimeBegin='00:00:00:11' "
                       "titleTimeEnd='00:00:00:30'/></Title>"
    /* 2: a map that is not on the disc. */
    TITLE("missing") CLIP "MISSING.MAP' titleTimeBegin='00:00:00:00' "
                          "titleTimeEnd='00:00:00:10'/></Title>"
    /* 3: no primary clips. */
    TITLE("empty") "</Title>"
    /* 4: a map naming an EVOB the VTI does not list. */
    TITLE("orphan") CLIP "THREE.MAP' titleTimeBegin='00:00:00:00' "
                         "titleTimeEnd='00:00:00:10'/></Title>"
    /* 5: no titleTimeEnd. */
    TITLE("open") CLIP "ONE.MAP' titleTimeBegin='00:00:00:00'/></Title>"
                       "</TitleSet></Playlist>";

static int open_title(hddvd_ctx *ctx, const hddvd_xpl *xpl, size_t title, hddvd_title_map **map)
{
    *map = (hddvd_title_map *)ctx;
    return hddvd_title_map_open(ctx, xpl, title, map);
}

static void at(const hddvd_title_map *map, uint64_t ticks, hddvd_title_position *p)
{
    hddvd_time t;
    t.ticks = ticks;
    assert(hddvd_title_map_locate(map, t, p) == HDDVD_OK);
}

static void test_conversion(void)
{
    hddvd_xpl_time x;
    hddvd_time t;
    x.frame_rate = 60;
    x.frames = 720;
    /* 12 s of 60000/1001 frames. */
    assert(hddvd_xpl_time_to_ticks(x, &t) == HDDVD_OK && t.ticks == 1081080);
    x.frames = 1;
    assert(hddvd_xpl_time_to_ticks(x, &t) == HDDVD_OK && t.ticks == 1501);
    x.frames = UINT64_MAX / 3003u + 1;
    assert(hddvd_xpl_time_to_ticks(x, &t) == HDDVD_ELIMIT);
    x.frame_rate = 50;
    x.frames = 1;
    assert(hddvd_xpl_time_to_ticks(x, &t) == HDDVD_ENOTSUP);
    assert(hddvd_xpl_time_to_ticks(x, NULL) == HDDVD_EINVAL);
}

int main(void)
{
    static const uint32_t one[][3] = {{2, 40, 10}, {3, 40, 20}};
    static const uint32_t two[][3] = {{1, 80, 30}, {1, 80, 30}};
    static const uint32_t three[][3] = {{1, 80, 30}};
    hddvd_ctx *ctx;
    hddvd_xpl *xpl;
    hddvd_title_map *map;
    hddvd_title_position p;
    hddvd_error_detail detail;
    hddvd_time t;

    test_conversion();
    MKDIR(ROOT);
    MKDIR(ROOT "/HVDVD_TS");
    write_map("ONE.MAP", 1, one, 2);
    write_map("TWO.MAP", 2, two, 2);
    write_map("THREE.MAP", 3, three, 1);
    write_vti();
    assert(hddvd_xpl_open(playlist, sizeof(playlist) - 1, &xpl) == HDDVD_OK);
    ctx = hddvd_open(ROOT);
    assert(ctx);

    assert(open_title(ctx, xpl, 0, &map) == HDDVD_OK);
    at(map, 0, &p);
    assert(p.clip == 0 && !strcmp(p.evo_uri, "file:///dvddisc/HVDVD_TS/ONE.EVO"));
    assert(p.evob_number == 1 && p.evob_offset.ticks == 0 && p.pts.ticks == 1000);
    assert(p.entry == 0 && p.sector == 0 && p.entry_pts.ticks == 1000);
    /* 10 frames in: the second EVOBU, which starts there. */
    at(map, 15015, &p);
    assert(p.entry == 1 && p.sector == 10 && p.entry_pts.ticks == 1000 + 15015);
    at(map, 15014, &p);
    assert(p.entry == 0);
    /* The second clip starts 10 frames into TWO.EVO. */
    at(map, 30030, &p);
    assert(p.clip == 1 && !strcmp(p.evo_uri, "file:///dvddisc/HVDVD_TS/TWO.EVO"));
    assert(p.evob_number == 2 && p.evob_offset.ticks == 15015 && p.pts.ticks == 2000 + 15015);
    assert(p.entry == 0 && p.sector == 0 && p.entry_pts.ticks == 2000);
    at(map, 75074, &p);
    assert(p.clip == 1 && p.evob_offset.ticks == 60059 && p.entry == 1 && p.sector == 30);
    /* The last 10 frames of the title have no clip. */
    t.ticks = 75075;
    assert(hddvd_title_map_locate(map, t, &p) == HDDVD_ENOTFOUND);
    assert(hddvd_title_map_locate(NULL, t, &p) == HDDVD_EINVAL);
    assert(hddvd_title_map_locate(map, t, NULL) == HDDVD_EINVAL);
    hddvd_title_map_close(map);

    assert(open_title(ctx, xpl, 1, &map) == HDDVD_EPARSE && map == NULL);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK &&
           detail.subsystem == HDDVD_SUBSYSTEM_TIME_MAP);
    assert(open_title(ctx, xpl, 2, &map) == HDDVD_ENOTFOUND && map == NULL);
    assert(open_title(ctx, xpl, 3, &map) == HDDVD_OK);
    t.ticks = 0;
    assert(hddvd_title_map_locate(map, t, &p) == HDDVD_ENOTFOUND);
    hddvd_title_map_close(map);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == HDDVD_OK);
    assert(open_title(ctx, xpl, 4, &map) == HDDVD_EPARSE && map == NULL);
    assert(open_title(ctx, xpl, 5, &map) == HDDVD_EPARSE && map == NULL);
    assert(open_title(ctx, xpl, 6, &map) == HDDVD_EINVAL && map == NULL);
    assert(hddvd_title_map_open(NULL, xpl, 0, &map) == HDDVD_EINVAL);
    assert(hddvd_title_map_open(ctx, xpl, 0, NULL) == HDDVD_EINVAL);
    hddvd_title_map_close(NULL);

    /* Without the VTI, the EVO files cannot be named. */
    assert(remove(DISC_DIR "HVA00001.VTI") == 0);
    assert(open_title(ctx, xpl, 0, &map) == HDDVD_ENOTFOUND && map == NULL);

    hddvd_close(ctx);
    hddvd_xpl_close(xpl);
    remove(DISC_DIR "ONE.MAP");
    remove(DISC_DIR "TWO.MAP");
    remove(DISC_DIR "THREE.MAP");
    RMDIR(ROOT "/HVDVD_TS");
    RMDIR(ROOT);
    return 0;
}
