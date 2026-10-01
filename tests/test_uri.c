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

static void put16(FILE *file, unsigned value)
{
    fputc((value >> 8) & 255, file);
    fputc(value & 255, file);
}
static void put32(FILE *file, unsigned value)
{
    fputc((value >> 24) & 255, file);
    fputc((value >> 16) & 255, file);
    fputc((value >> 8) & 255, file);
    fputc(value & 255, file);
}
static unsigned crc32(const unsigned char *data, size_t size)
{
    unsigned crc = ~0u;
    size_t i;
    for (i = 0; i < size; i++)
    {
        unsigned k;
        crc ^= data[i];
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return ~crc;
}
static void write_archive(const char *path)
{
    const unsigned char payload[] = "member";
    const char name[] = "Item.bin";
    unsigned offset = 32 + 46 + 8;
    FILE *file = fopen(path, "wb");
    unsigned i;
    assert(file);
    fwrite("HDDVDACA", 1, 8, file);
    put16(file, 0x10);
    put16(file, 1);
    put16(file, 1);
    put16(file, 0x35);
    for (i = 0; i < 16; i++)
        fputc(0, file);
    put32(file, offset);
    put32(file, sizeof(payload) - 1);
    put32(file, crc32(payload, sizeof(payload) - 1));
    fputc(0xff, file);
    fputc(8, file);
    fwrite(name, 1, 8, file);
    for (i = 0; i < 32; i++)
        fputc(0, file);
    fwrite(payload, 1, sizeof(payload) - 1, file);
    fclose(file);
}

int main(void)
{
    hddvd_uri *uri = NULL;
    hddvd_ctx *ctx;
    hddvd_stream *stream;
    char data[4] = {0};
    size_t got;
    FILE *file;

    assert(hddvd_uri_parse("file:///dvddisc/ADV_OBJ/archived.aca/main.xmf", &uri) == HDDVD_OK);
    assert(!strcmp(hddvd_uri_disc_path(uri), "ADV_OBJ/archived.aca/main.xmf"));
    assert(!strcmp(hddvd_uri_archive_name(uri), "ADV_OBJ/archived.aca"));
    assert(!strcmp(hddvd_uri_member_name(uri), "main.xmf"));
    hddvd_uri_free(uri);
    assert(hddvd_uri_parse("file:///dvddisc/ADV_OBJ/%2e%2e/file", &uri) == HDDVD_EPARSE);
    assert(hddvd_uri_parse("file:///dvddisc/ADV_OBJ/a%2fb", &uri) == HDDVD_EPARSE);
    assert(hddvd_uri_parse("file:///dvddisc/ADV_OBJ/a%3fb", &uri) == HDDVD_EPARSE);
    assert(hddvd_uri_parse("file:///dvddisc/ADV_OBJ//file", &uri) == HDDVD_EPARSE);
    assert(hddvd_uri_parse("http://dvddisc/ADV_OBJ/a", &uri) == HDDVD_EPARSE);
    assert(hddvd_uri_parse("file:///dvddisc/", &uri) == HDDVD_EPARSE);

    assert(MKDIR("uri-disc") == 0);
    assert(MKDIR("uri-disc/ADV_OBJ") == 0);
    file = fopen("uri-disc/ADV_OBJ/Mixed.bin", "wb");
    assert(file && fwrite("data", 1, 4, file) == 4);
    fclose(file);
    write_archive("uri-disc/ADV_OBJ/Menu.aca");
    ctx = hddvd_open("uri-disc");
    assert(ctx);
    assert(hddvd_open_resource(ctx, "file:///dvddisc/adv_obj/mixed.BIN", &stream) == HDDVD_OK);
    assert(hddvd_stream_read_at(stream, 0, data, sizeof(data), &got) == HDDVD_OK);
    assert(got == 4 && !memcmp(data, "data", 4));
    hddvd_stream_close(stream);
    assert(hddvd_open_resource(ctx, "file:///dvddisc/ADV_OBJ/menu.ACA/item.BIN", &stream) ==
           HDDVD_OK);
    assert(hddvd_stream_read_at(stream, 0, data, sizeof(data), &got) == HDDVD_OK);
    assert(got == 4 && !memcmp(data, "memb", 4));
    hddvd_stream_close(stream);
    assert(hddvd_open_resource(ctx, "file:///dvddisc/../Mixed.bin", &stream) == HDDVD_EPARSE);
    hddvd_close(ctx);
    remove("uri-disc/ADV_OBJ/Mixed.bin");
    remove("uri-disc/ADV_OBJ/Menu.aca");
    assert(RMDIR("uri-disc/ADV_OBJ") == 0);
    assert(RMDIR("uri-disc") == 0);
    return 0;
}
