/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stdio.h>
#include <string.h>

typedef struct output_ctx
{
    int json;
    int first;
    size_t count;
} output_ctx;

static void json_string(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    putchar('"');
    while (*p)
    {
        switch (*p)
        {
            case '"':
                fputs("\\\"", stdout);
                break;
            case '\\':
                fputs("\\\\", stdout);
                break;
            case '\b':
                fputs("\\b", stdout);
                break;
            case '\f':
                fputs("\\f", stdout);
                break;
            case '\n':
                fputs("\\n", stdout);
                break;
            case '\r':
                fputs("\\r", stdout);
                break;
            case '\t':
                fputs("\\t", stdout);
                break;
            default:
                if (*p < 0x20)
                    printf("\\u%04x", (unsigned)*p);
                else
                    putchar(*p);
        }
        p++;
    }
    putchar('"');
}

static int show(void *opaque, const hddvd_aca_entry *entry)
{
    output_ctx *ctx = opaque;
    if (!ctx->json)
    {
        printf("%-24s %10u bytes  off=0x%08x crc=%08x flags=%02x\n", entry->name, entry->size,
               entry->offset, entry->crc32, entry->flags);
    }
    else
    {
        if (!ctx->first)
            fputs(",\n", stdout);
        fputs("    {\"name\":", stdout);
        json_string(entry->name);
        printf(",\"size\":%u,\"offset\":%u,\"crc32\":\"%08x\","
               "\"flags\":%u}",
               entry->size, entry->offset, entry->crc32, entry->flags);
        ctx->first = 0;
    }
    ctx->count++;
    return 0;
}

int main(int argc, char **argv)
{
    output_ctx ctx = {0, 1, 0};
    const char *path;
    size_t bad = 0;
    int rc;

    if (argc == 3 && !strcmp(argv[1], "--json"))
    {
        ctx.json = 1;
        path = argv[2];
    }
    else if (argc == 2)
    {
        path = argv[1];
    }
    else
    {
        fprintf(stderr, "Usage: %s [--json] archive.aca\n", argv[0]);
        return 2;
    }

    if (ctx.json)
        fputs("{\n  \"entries\": [\n", stdout);
    rc = hddvd_aca_list(path, show, &ctx);
    if (rc)
    {
        fprintf(stderr, "ACA parse failed: %s\n", hddvd_strerror(rc));
        return 1;
    }
    rc = hddvd_aca_verify(path, &bad);
    if (ctx.json)
    {
        printf("\n  ],\n  \"entry_count\": %zu,\n  \"crc_bad_count\": %zu,\n"
               "  \"crc_ok\": %s\n}\n",
               ctx.count, bad, rc == HDDVD_OK ? "true" : "false");
    }
    else
    {
        printf("CRC verification: %s (%zu bad)\n", rc == HDDVD_OK ? "OK" : "FAILED", bad);
    }
    return rc == HDDVD_OK ? 0 : 1;
}
