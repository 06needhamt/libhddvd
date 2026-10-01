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
    if (!f) {
        return 1;
    }
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
    if (hddvd_aca_verify(fn, &bad) || bad) {
        return 2;
    }
    if (hddvd_aca_read_member(fn, name, &b, &n) || n != sizeof(data) - 1 || memcmp(b, data, n)) {
        return 3;
    }
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
    if (hddvd_stream_open_file(fn, &file_stream)) {
        return 6;
    }
    hddvd_limits_default(&limits);
    limits.max_aca_member_size = 1;
    if (hddvd_aca_open_stream_with_limits(file_stream, &limits, &a) != HDDVD_ELIMIT) {
        return 7;
    }
    hddvd_stream_close(file_stream);
    remove(fn);
    return 0;
}
