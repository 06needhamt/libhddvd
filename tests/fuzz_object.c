/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_object *object = NULL;
    (void)hddvd_object_open(data, size, &object);
    hddvd_object_close(object);
    return 0;
}
