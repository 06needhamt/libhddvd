#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>

/* Resolves title times to the EVO file and sector that play them, using the
   playlist's clips and the time maps and VTI on the disc. */
int main(int argc, char **argv)
{
    FILE *f;
    long end;
    unsigned char *bytes;
    hddvd_ctx *ctx;
    hddvd_xpl *xpl = NULL;
    hddvd_title_map *map = NULL;
    hddvd_xpl_title_info info;
    hddvd_error_detail detail;
    size_t title;
    int a, rc, failed = 0;
    if (argc < 5)
    {
        fprintf(stderr, "usage: %s DISC_ROOT PLAYLIST.XPL TITLE_INDEX MILLISECONDS...\n", argv[0]);
        return 2;
    }
    f = fopen(argv[2], "rb");
    if (!f || fseek(f, 0, SEEK_END) || (end = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET))
    {
        fprintf(stderr, "%s: cannot read playlist\n", argv[0]);
        if (f)
            fclose(f);
        return 1;
    }
    bytes = malloc((size_t)end);
    if (!bytes || fread(bytes, 1, (size_t)end, f) != (size_t)end)
    {
        fprintf(stderr, "%s: cannot read playlist\n", argv[0]);
        free(bytes);
        fclose(f);
        return 1;
    }
    fclose(f);
    rc = hddvd_xpl_open(bytes, (size_t)end, &xpl);
    free(bytes);
    if (rc)
    {
        fprintf(stderr, "%s: %s\n", argv[2], hddvd_strerror(rc));
        return 1;
    }
    title = (size_t)strtoul(argv[3], NULL, 10);
    ctx = hddvd_open(argv[1]);
    rc = ctx ? hddvd_title_map_open(ctx, xpl, title, &map) : HDDVD_ENOMEM;
    if (rc)
    {
        fprintf(stderr, "title %zu: %s", title, hddvd_strerror(rc));
        if (ctx && hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.message[0])
            fprintf(stderr, " (%s: %s)", detail.resource, detail.message);
        fprintf(stderr, "\n");
        hddvd_close(ctx);
        hddvd_xpl_close(xpl);
        return 1;
    }
    hddvd_xpl_get_title(xpl, title, &info);
    for (a = 4; a < argc; a++)
    {
        hddvd_time t;
        hddvd_title_position p;
        rc = hddvd_time_from_milliseconds(strtoull(argv[a], NULL, 10), &t);
        if (!rc)
            rc = hddvd_title_map_locate(map, t, &p);
        if (rc)
        {
            printf("%s %sms: %s\n", info.id ? info.id : "(title)", argv[a], hddvd_strerror(rc));
            failed = 1;
            continue;
        }
        printf("%s %sms: clip=%zu %s evob=%u pts=%llu evobu=%zu sector=%lu evobu_pts=%llu\n",
               info.id ? info.id : "(title)", argv[a], p.clip, p.evo_uri, (unsigned)p.evob_number,
               (unsigned long long)p.pts.ticks, p.entry, (unsigned long)p.sector,
               (unsigned long long)p.entry_pts.ticks);
    }
    hddvd_title_map_close(map);
    hddvd_close(ctx);
    hddvd_xpl_close(xpl);
    return failed;
}
