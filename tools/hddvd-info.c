/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stdio.h>
static int show(void *o, const hddvd_file_info *f)
{
    (void)o;
    printf("%-6s %10llu  %s\n", hddvd_file_kind_name(f->kind), (unsigned long long)f->size,
           f->path);
    if (f->kind == HDDVD_FILE_EVO)
    {
        hddvd_stream_info s[32];
        size_t n = 0, i;
        if (hddvd_evo_probe(f->path, s, 32, &n) == HDDVD_OK)
            for (i = 0; i < n; i++)
                printf("       stream %02x/%02x %-14s packets=%llu\n", s[i].stream_id,
                       s[i].substream_id, hddvd_codec_name(s[i].codec),
                       (unsigned long long)s[i].packet_count);
    }
    return 0;
}
int main(int argc, char **argv)
{
    hddvd_ctx *c;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <HD DVD directory>\n", argv[0]);
        return 2;
    }
    c = hddvd_open(argv[1]);
    if (!c)
    {
        fprintf(stderr, "not an HD DVD directory or cannot open: %s\n", argv[1]);
        return 1;
    }
    printf("libhddvd %s\nroot: %s\nadvanced-content: %s\n", hddvd_version(), hddvd_root(c),
           hddvd_has_advanced_content(c) ? "yes" : "no");
    hddvd_scan_files(c, show, NULL);
    hddvd_close(c);
    return 0;
}
