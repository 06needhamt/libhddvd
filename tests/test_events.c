/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdint.h>
int main(void)
{
    hddvd_time t;
    uint64_t ms = 0;
    assert(hddvd_time_from_milliseconds(1234, &t) == HDDVD_OK && t.ticks == 111060);
    assert(hddvd_time_to_milliseconds(t, &ms) == HDDVD_OK && ms == 1234);
    assert(hddvd_time_from_seconds(2, &t) == HDDVD_OK && t.ticks == 180000);
    assert(hddvd_time_from_seconds(UINT64_MAX, &t) == HDDVD_EINVAL);
    return 0;
}
