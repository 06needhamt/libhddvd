#include "hddvd.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* hddvd_titles over a disc created once per process: the Standard Content
   fixture (tests/fixtures/standard) plus ONE.MAP, a VTI and ONE.EVO for a
   playlist title, and the menu EVOs of tests/fixtures/guesses. The input's
   first byte picks the file the rest replaces, so each part of a disc is
   fuzzed against valid neighbours:
     0  both EVOs: the input's sectors, repeated to fill the file;
     1  the title set IFO;  2  the VMG;  3  ONE.MAP;  4  the playlist;
     5  the VMG's menu EVO.
   Every title is then listed and read through, and the Standard Content
   navigator (hddvd_std_nav) follows the disc's commands, without and with
   its guesses (menu cells and their buttons, angle blocks, PGC links). */
#define SECTOR 2048u
#define EVO_SECTORS 36u

static char root[64];

static void put32(unsigned char *b, uint32_t v)
{
    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);
    b[3] = (unsigned char)v;
}

static void write_file(const char *name, const void *data, size_t size)
{
    char path[160];
    FILE *f;
    snprintf(path, sizeof(path), "%s/HVDVD_TS/%s", root, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(data, 1, size, f);
    fclose(f);
}

typedef struct blob
{
    unsigned char *bytes;
    size_t size;
} blob;

static blob fixture(const char *name)
{
    char path[512];
    blob out = {NULL, 0};
    FILE *f;
    long end;
    snprintf(path, sizeof(path), "%s/%s", HDDVD_FIXTURE_DIR, name);
    f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END) || (end = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET))
        abort();
    out.bytes = malloc((size_t)end);
    if (!out.bytes || fread(out.bytes, 1, (size_t)end, f) != (size_t)end)
        abort();
    out.size = (size_t)end;
    fclose(f);
    return out;
}

static blob vmg, vts, evo, vmg_menu, vts_menu;
static unsigned char map[2048], vti[4096];
static const char playlist[] =
    "<Playlist xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Playlist'>"
    "<TitleSet timeBase='60fps'><Title id='t' titleDuration='00:00:01:00'>"
    "<PrimaryAudioVideoClip src='file:///dvddisc/HVDVD_TS/ONE.MAP' "
    "titleTimeBegin='00:00:00:00' titleTimeEnd='00:00:00:20'/>"
    "<PrimaryAudioVideoClip src='file:///dvddisc/HVDVD_TS/ONE.MAP' "
    "titleTimeBegin='00:00:00:20' clipTimeBegin='00:00:00:10' titleTimeEnd='00:00:00:40'/>"
    "<ChapterList><Chapter titleTimeBegin='00:00:00:00'/>"
    "<Chapter titleTimeBegin='00:00:00:25'/><Chapter titleTimeBegin='00:00:00:50'/>"
    "</ChapterList></Title></TitleSet></Playlist>";

/* ONE.MAP is EVOB 1: two EVOBUs of 20 frames, 10 and 20 sectors. */
static void setup(void)
{
    unsigned char *t = vti + 2048, *r = t + 12;
    char path[128];
    snprintf(root, sizeof(root), "/tmp/libhddvd-fuzz-titles-%ld", (long)getpid());
    mkdir(root, 0755);
    snprintf(path, sizeof(path), "%s/HVDVD_TS", root);
    mkdir(path, 0755);
    vmg = fixture("standard/HVDVD_TS/HV000I01.IFO");
    vts = fixture("standard/HVDVD_TS/HV001I01.IFO");
    evo = fixture("standard/HVDVD_TS/HV001T01.EVO");
    vmg_menu = fixture("guesses/HVDVD_TS/HV000M01.EVO");
    vts_menu = fixture("guesses/HVDVD_TS/HV001M01.EVO");
    write_file("HV001M01.EVO", vts_menu.bytes, vts_menu.size);

    memcpy(map, "HDDVD_TMAP00", 12);
    memcpy(map + 0x72, "HVA00001.VTI", 12);
    put32(map + 0x180, 0x1a0);
    map[0x185] = 1;
    map[0x187] = 2;
    put32(map + 0x1a0, 2u << 21 | 80u << 11 | 10u);
    put32(map + 0x1a4, 3u << 21 | 80u << 11 | 20u);

    memcpy(vti, "ADVANCED-VTS", 12);
    put32(vti + 0x1c, 1);
    put32(vti + 0xbc, 1);
    t[3] = 1;
    put32(t + 4, 12 + 0x140 - 1);
    put32(t + 8, 12);
    memcpy(r + 2, "ONE.EVO", 7);
    r[0x109] = 1;
    put32(r + 0x10a, 1000);
    put32(r + 0x10e, 1000 + 60060);
    put32(r + 0x112, 30);
    write_file("HVA00001.VTI", vti, sizeof(vti));
}

/* A host provider that claims every protected sector is now clear: it
   clears the scrambling bits of the packet after a 14-byte pack header. */
static int provide(void *opaque, const char *evo_uri, uint32_t sector, uint8_t data[2048])
{
    (void)opaque;
    (void)evo_uri;
    if (sector & 1u)
        return HDDVD_EPROTECTED;
    data[20] &= 0xcf;
    return HDDVD_OK;
}

static void read_through(hddvd_titles *titles)
{
    unsigned char buffer[5000];
    size_t title;
    (void)hddvd_titles_set_sector_provider(titles, provide, NULL);
    for (title = 0; title < hddvd_titles_count(titles) && title < 8; title++)
    {
        hddvd_title_info info;
        hddvd_title_chapter chapter;
        hddvd_title_extent extent;
        uint64_t offset = 0;
        size_t i, got = 0;
        if (hddvd_titles_get_info(titles, title, &info) != HDDVD_OK)
            continue;
        for (i = 0; i < info.chapter_count; i++)
            if (hddvd_titles_get_chapter(titles, title, i, &chapter) == HDDVD_OK)
                (void)hddvd_titles_chapter_at(titles, title, chapter.offset, &i);
        for (i = 0; i < info.extent_count; i++)
            (void)hddvd_titles_get_extent(titles, title, i, &extent);
        /* Unaligned reads, so sectors are also entered part-way. */
        while (offset < info.size && offset < 64u * SECTOR &&
               hddvd_titles_read(titles, title, offset, buffer, sizeof(buffer), &got) == HDDVD_OK &&
               got)
            offset += got;
        /* Past a sector that failed, the rest is still reachable. */
        (void)hddvd_titles_read(titles, title, info.size / 2 + 7, buffer, 300, &got);
    }
}

/* A menu cell: read part of it, look at its buttons, and press one. */
static int press(hddvd_std_nav *nav, unsigned round, hddvd_std_nav_step *step)
{
    hddvd_std_nav_menu_cell cell;
    hddvd_std_nav_button button;
    unsigned char buffer[300];
    size_t got, i, count = hddvd_std_nav_button_count(nav);
    (void)hddvd_std_nav_get_menu_cell(nav, &cell);
    (void)hddvd_std_nav_read_menu(nav, (uint64_t)round * 777u, buffer, sizeof(buffer), &got);
    for (i = 0; i < count; i++)
        (void)hddvd_std_nav_get_button(nav, i, &button);
    if (!count)
        return hddvd_std_nav_next(nav, step);
    (void)hddvd_std_nav_select_button(nav, 1u + round % (unsigned)count);
    (void)hddvd_std_nav_selected_button(nav);
    return hddvd_std_nav_activate_button(nav, step);
}

/* Follows the disc's own navigation for a bounded number of cells, then
   the user's choices. */
static void navigate(hddvd_ctx *ctx, unsigned options)
{
    hddvd_std_nav *nav = NULL;
    hddvd_std_nav_step step;
    unsigned char buffer[300];
    size_t got;
    unsigned i;
    if (hddvd_std_nav_open(ctx, &nav) != HDDVD_OK)
        return;
    (void)hddvd_std_nav_set_options(nav, options);
    (void)hddvd_std_nav_set_angle(nav, 2);
    if (hddvd_std_nav_start(nav, &step) == HDDVD_OK)
        for (i = 0; i < 16 && (step.kind == HDDVD_STD_NAV_PLAY || step.kind == HDDVD_STD_NAV_MENU);
             i++)
        {
            int rc;
            if (step.kind == HDDVD_STD_NAV_MENU)
                rc = press(nav, i, &step);
            else
            {
                (void)hddvd_titles_read(hddvd_std_nav_titles(nav), step.title, step.offset, buffer,
                                        sizeof(buffer), &got);
                rc = hddvd_std_nav_next(nav, &step);
            }
            if (rc != HDDVD_OK)
                break;
        }
    for (i = 0; i < 3; i++)
    {
        if (hddvd_std_nav_select_title(nav, i, i & 1u, &step) == HDDVD_OK &&
            step.kind == HDDVD_STD_NAV_PLAY)
            (void)hddvd_std_nav_next(nav, &step);
        (void)hddvd_std_nav_menu(nav, 2 + i, &step);
        if (hddvd_std_nav_seek(nav, i, (uint64_t)i * 5000u, &step) == HDDVD_OK &&
            step.kind == HDDVD_STD_NAV_PLAY)
            (void)hddvd_std_nav_next(nav, &step);
    }
    hddvd_std_nav_close(nav);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static hddvd_ctx *ctx;
    static unsigned char sectors[EVO_SECTORS * SECTOR];
    hddvd_titles *titles = NULL;
    hddvd_xpl *xpl = NULL;
    unsigned part;
    if (!ctx)
    {
        setup();
        ctx = hddvd_open(root);
    }
    if (!ctx || !size)
        return 0;
    part = data[0] % 6u;
    data++;
    size--;

    if (part == 0 && size)
    {
        size_t count = (size + SECTOR - 1) / SECTOR, i;
        memset(sectors, 0, sizeof(sectors));
        for (i = 0; i < EVO_SECTORS; i++)
        {
            size_t from = (i % count) * SECTOR;
            memcpy(sectors + i * SECTOR, data + from, size - from < SECTOR ? size - from : SECTOR);
        }
        write_file("HV001T01.EVO", sectors, sizeof(sectors));
        write_file("ONE.EVO", sectors, sizeof(sectors));
    }
    else
    {
        write_file("HV001T01.EVO", evo.bytes, evo.size);
        write_file("ONE.EVO", evo.bytes, evo.size);
    }
    write_file("HV001I01.IFO", part == 1 ? data : vts.bytes, part == 1 ? size : vts.size);
    write_file("HV000I01.IFO", part == 2 ? data : vmg.bytes, part == 2 ? size : vmg.size);
    write_file("ONE.MAP", part == 3 ? data : map, part == 3 ? size : sizeof(map));
    write_file("HV000M01.EVO", part == 5 ? data : vmg_menu.bytes, part == 5 ? size : vmg_menu.size);

    if (hddvd_titles_open(ctx, &titles) == HDDVD_OK)
        read_through(titles);
    hddvd_titles_close(titles);
    titles = NULL;
    navigate(ctx, 0);
    navigate(ctx, HDDVD_STD_NAV_GUESS_PGC_LINKS | HDDVD_STD_NAV_GUESS_ANGLES |
                      HDDVD_STD_NAV_GUESS_MENU_CELLS);
    if (hddvd_xpl_open(part == 4 ? (const void *)data : (const void *)playlist,
                       part == 4 ? size : sizeof(playlist) - 1, &xpl) == HDDVD_OK &&
        hddvd_titles_open_playlist(ctx, xpl, &titles) == HDDVD_OK)
        read_through(titles);
    hddvd_titles_close(titles);
    hddvd_xpl_close(xpl);
    return 0;
}
