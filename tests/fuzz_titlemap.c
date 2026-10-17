#include "hddvd.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    if (!f)
        return;
    fwrite(data, 1, size, f);
    fclose(f);
}

/* A synthetic disc, created once per process: ONE.MAP (EVOB 1, two EVOBUs of
   20 frames) and a VTI listing ONE.EVO. The fuzzed input is the playlist, so
   clip sources, times and offsets drive the title map. */
static hddvd_ctx *disc(void)
{
    static hddvd_ctx *ctx;
    char root[64], path[128];
    unsigned char map[2048] = {0}, vti[4096] = {0}, *t = vti + 2048, *r = t + 12;
    if (ctx)
        return ctx;
    snprintf(root, sizeof(root), "/tmp/libhddvd-fuzz-titlemap-%ld", (long)getpid());
    mkdir(root, 0755);
    snprintf(path, sizeof(path), "%s/HVDVD_TS", root);
    mkdir(path, 0755);

    memcpy(map, "HDDVD_TMAP00", 12);
    memcpy(map + 0x72, "HVA00001.VTI", 12);
    put32(map + 0x180, 0x1a0);
    map[0x185] = 1;
    map[0x187] = 2;
    put32(map + 0x1a0, 2u << 21 | 80u << 11 | 10u);
    put32(map + 0x1a4, 3u << 21 | 80u << 11 | 20u);
    snprintf(path, sizeof(path), "%s/HVDVD_TS/ONE.MAP", root);
    write_file(path, map, sizeof(map));

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
    snprintf(path, sizeof(path), "%s/HVDVD_TS/HVA00001.VTI", root);
    write_file(path, vti, sizeof(vti));
    ctx = hddvd_open(root);
    return ctx;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_ctx *ctx = disc();
    hddvd_xpl *xpl = NULL;
    size_t i;
    if (!ctx || hddvd_xpl_open(data, size, &xpl) != HDDVD_OK)
        return 0;
    for (i = 0; i < hddvd_xpl_title_count(xpl) && i < 8; i++)
    {
        hddvd_title_map *map = NULL;
        hddvd_title_position position;
        hddvd_xpl_title_info info;
        hddvd_time t;
        if (hddvd_title_map_open(ctx, xpl, i, &map) != HDDVD_OK)
            continue;
        t.ticks = 0;
        (void)hddvd_title_map_locate(map, t, &position);
        t.ticks = 30029;
        (void)hddvd_title_map_locate(map, t, &position);
        if (hddvd_xpl_get_title(xpl, i, &info) == HDDVD_OK && info.has_duration &&
            hddvd_xpl_time_to_ticks(info.duration, &t) == HDDVD_OK)
        {
            (void)hddvd_title_map_locate(map, t, &position);
            t.ticks /= 2;
            (void)hddvd_title_map_locate(map, t, &position);
        }
        hddvd_title_map_close(map);
    }
    hddvd_xpl_close(xpl);
    return 0;
}
