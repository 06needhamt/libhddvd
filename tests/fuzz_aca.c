/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int visit(void *opaque, const hddvd_aca_entry *entry)
{
    (void)opaque;
    (void)entry;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char path[128];
    FILE *file;
    size_t bad;

    snprintf(path, sizeof(path), "/tmp/libhddvd-fuzz-%ld.aca", (long)getpid());
    file = fopen(path, "wb");
    if (!file)
        return 0;
    if (size && fwrite(data, 1, size, file) != size)
    {
        fclose(file);
        remove(path);
        return 0;
    }
    fclose(file);
    (void)hddvd_aca_list(path, visit, NULL);
    (void)hddvd_aca_verify(path, &bad);
    remove(path);

    hddvd_stream *stream;
    hddvd_aca *archive;
    if (hddvd_stream_open_memory(data, size, &stream) != HDDVD_OK)
        return 0;
    if (hddvd_aca_open_stream(stream, &archive) == HDDVD_OK)
    {
        size_t i;
        for (i = 0; i < hddvd_aca_count(archive); i++)
        {
            hddvd_aca_entry entry;
            if (hddvd_aca_get_entry(archive, i, &entry) == HDDVD_OK)
                (void)visit(NULL, &entry);
        }
        hddvd_aca_close(archive);
    }
    hddvd_stream_close(stream);
    return 0;
}
