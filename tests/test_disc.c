/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
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

static int visit(void *opaque, const hddvd_file_info *file)
{
    unsigned *count = opaque;
    assert(file->kind == HDDVD_FILE_EVO);
    assert(file->size == 4);
    assert(strstr(file->path, "movie.evo") != NULL);
    ++*count;
    return 0;
}

int main(void)
{
    hddvd_ctx *ctx;
    FILE *file;
    unsigned count = 0;
    assert(MKDIR("scan-disc") == 0);
    ctx = hddvd_open("scan-disc");
    assert(ctx);
    assert(hddvd_scan_files(ctx, visit, &count) == HDDVD_ENOTFOUND);
    assert(MKDIR("scan-disc/HVDVD_TS") == 0);
    assert(hddvd_scan_files(ctx, visit, &count) == HDDVD_OK);
    assert(count == 0);
    /* A directory with a recognized suffix must not be returned as a file. */
    assert(MKDIR("scan-disc/HVDVD_TS/directory.evo") == 0);
    file = fopen("scan-disc/HVDVD_TS/movie.evo", "wb");
    assert(file);
    assert(fwrite("data", 1, 4, file) == 4);
    assert(fclose(file) == 0);
    assert(hddvd_scan_files(ctx, visit, &count) == HDDVD_OK);
    assert(count == 1);
    hddvd_close(ctx);
    assert(remove("scan-disc/HVDVD_TS/movie.evo") == 0);
    assert(RMDIR("scan-disc/HVDVD_TS/directory.evo") == 0);
    assert(RMDIR("scan-disc/HVDVD_TS") == 0);
    assert(RMDIR("scan-disc") == 0);
    return 0;
}
