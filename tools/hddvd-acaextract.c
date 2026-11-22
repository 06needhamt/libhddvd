/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stdio.h>
int main(int argc, char **argv)
{
    int rc;
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s archive.aca output-dir\n", argv[0]);
        return 2;
    }
    rc = hddvd_aca_extract(argv[1], argv[2]);
    if (rc)
    {
        fprintf(stderr, "Extract failed: %s\n", hddvd_strerror(rc));
        return 1;
    }
    return 0;
}
