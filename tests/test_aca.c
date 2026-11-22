/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put16(FILE *f, unsigned v)
{
    fputc((v >> 8) & 255, f);
    fputc(v & 255, f);
}
static void put32(FILE *f, unsigned v)
{
    fputc((v >> 24) & 255, f);
    fputc((v >> 16) & 255, f);
    fputc((v >> 8) & 255, f);
    fputc(v & 255, f);
}
static unsigned crc(const unsigned char *p, size_t n)
{
    unsigned c = ~0u;
    size_t i;
    for (i = 0; i < n; i++)
    {
        unsigned k;
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ ((0u - (c & 1u)) & 0xedb88320u);
    }
    return ~c;
}
/* Two members, each preceded by an unlisted block of non-zero bytes, as in
   archives that keep each member's envelope header outside its entry. */
static size_t build_gapped(unsigned char *out, unsigned gap, unsigned first_offset)
{
    static const unsigned char data[2][4] = {{'o', 'n', 'e', '!'}, {'t', 'w', 'o', '!'}};
    static const char *names[2] = {"one.png", "two.png"};
    size_t pos = 32, directory_end = 32 + 2 * (46 + 7), i, at;
    memset(out, 0, 512);
    memcpy(out, "HDDVDACA", 8);
    out[9] = 0x10;
    out[11] = 1;
    out[13] = 2;
    out[15] = 0x35;
    at = first_offset ? first_offset : directory_end + gap;
    for (i = 0; i < 2; i++)
    {
        unsigned crc_value = crc(data[i], 4);
        size_t k;
        out[pos] = (unsigned char)(at >> 24);
        out[pos + 1] = (unsigned char)(at >> 16);
        out[pos + 2] = (unsigned char)(at >> 8);
        out[pos + 3] = (unsigned char)at;
        out[pos + 7] = 4;
        for (k = 0; k < 4; k++)
            out[pos + 8 + k] = (unsigned char)(crc_value >> (24 - 8 * k));
        out[pos + 12] = 0xff;
        out[pos + 13] = 7;
        memcpy(out + pos + 14, names[i], 7);
        pos += 46 + 7;
        if (at >= gap)
            memset(out + at - gap, 'G', gap);
        memcpy(out + at, data[i], 4);
        at += 4 + gap;
    }
    return at - gap;
}

static int test_gapped_archive(void)
{
    unsigned char bytes[512];
    size_t size = build_gapped(bytes, 9, 0), got = 0;
    hddvd_stream *stream = NULL, *member = NULL;
    hddvd_aca *aca = NULL;
    char text[4];
    if (hddvd_stream_open_memory(bytes, size, &stream) || hddvd_aca_open_stream(stream, &aca) ||
        hddvd_aca_count(aca) != 2 || hddvd_aca_open_member(aca, "two.png", &member) ||
        hddvd_stream_read_at(member, 0, text, 4, &got) || got != 4 || memcmp(text, "two!", 4))
        return 1;
    hddvd_stream_close(member);
    hddvd_aca_close(aca);
    hddvd_stream_close(stream);
    /* A member may never start inside the directory. */
    size = build_gapped(bytes, 0, 40);
    if (hddvd_stream_open_memory(bytes, size, &stream) ||
        hddvd_aca_open_stream(stream, &aca) != HDDVD_EPARSE)
        return 2;
    hddvd_stream_close(stream);
    return 0;
}

int main(void)
{
    const unsigned char data[] = "<Application/>";
    const char *name = "main.xmf";
    const char *fn = "test.aca";
    FILE *f = fopen(fn, "wb");
    unsigned off = 32 + 46 + 8;
    hddvd_aca_entry e;
    hddvd_aca *a = NULL;
    hddvd_stream *s = NULL, *file_stream = NULL;
    hddvd_limits limits;
    size_t bad = 9;
    void *b = NULL;
    size_t n = 0;
    if (!f)
        return 1;
    fwrite("HDDVDACA", 1, 8, f);
    put16(f, 0x10);
    put16(f, 1);
    put16(f, 1);
    put16(f, 0x35);
    for (int i = 0; i < 16; i++)
        fputc(0, f);
    put32(f, off);
    put32(f, sizeof(data) - 1);
    put32(f, crc(data, sizeof(data) - 1));
    fputc(0xff, f);
    fputc(8, f);
    fwrite(name, 1, 8, f);
    for (int i = 0; i < 32; i++)
        fputc(0, f);
    fwrite(data, 1, sizeof(data) - 1, f);
    fclose(f);
    if (hddvd_aca_verify(fn, &bad) || bad)
        return 2;
    if (hddvd_aca_read_member(fn, name, &b, &n) || n != sizeof(data) - 1 || memcmp(b, data, n))
        return 3;
    hddvd_free(b);
    if (hddvd_aca_open(fn, &a) || hddvd_aca_count(a) != 1 || hddvd_aca_get_entry(a, 0, &e) ||
        strcmp(e.name, name) || hddvd_aca_open_member(a, "MAIN.XMF", &s))
        return 4;
    hddvd_aca_close(a);
    b = malloc(sizeof(data));
    if (!b || hddvd_stream_read_at(s, 0, b, sizeof(data), &n) || n != sizeof(data) - 1 ||
        memcmp(b, data, n))
        return 5;
    free(b);
    hddvd_stream_close(s);
    if (hddvd_stream_open_file(fn, &file_stream))
        return 6;
    hddvd_limits_default(&limits);
    limits.max_aca_member_size = 1;
    if (hddvd_aca_open_stream_with_limits(file_stream, &limits, &a) != HDDVD_ELIMIT)
        return 7;
    hddvd_stream_close(file_stream);
    remove(fn);
    if (test_gapped_archive())
        return 8;
    return 0;
}
