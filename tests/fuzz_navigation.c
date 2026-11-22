/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_stream *stream = NULL;
    hddvd_ifo_header ifo;
    hddvd_map_header map;
    if (hddvd_stream_open_memory(data, size, &stream) != HDDVD_OK)
        return 0;
    (void)hddvd_ifo_read_header(stream, &ifo);
    (void)hddvd_map_read_header(stream, &map);
    hddvd_stream_close(stream);
    return 0;
}
