/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_script *script = NULL;
    if (hddvd_script_open(data, size, &script) == HDDVD_OK)
        hddvd_script_close(script);
    return 0;
}
