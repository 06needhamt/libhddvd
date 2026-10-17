#include "hddvd.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A small disc root, created once per process: one file at the top level
   and one under ADV_OBJ. */
static hddvd_ctx *disc(void)
{
    static hddvd_ctx *ctx;
    static char root[64];
    char path[96];
    FILE *f;
    if (ctx)
        return ctx;
    snprintf(root, sizeof(root), "/tmp/libhddvd-fuzz-uri-%ld", (long)getpid());
    mkdir(root, 0755);
    snprintf(path, sizeof(path), "%s/ADV_OBJ", root);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/ADV_OBJ/menu.xmf", root);
    if ((f = fopen(path, "wb")))
    {
        fputs("<original/>", f);
        fclose(f);
    }
    snprintf(path, sizeof(path), "%s/DISCID.DAT", root);
    if ((f = fopen(path, "wb")))
    {
        fputs("original", f);
        fclose(f);
    }
    ctx = hddvd_open(root);
    return ctx;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char text[4096];
    hddvd_uri *uri = NULL;
    hddvd_stream *stream = NULL;
    hddvd_ctx *ctx = disc();
    if (size >= sizeof(text))
        return 0;
    memcpy(text, data, size);
    text[size] = 0;
    if (hddvd_uri_parse(text, &uri) == HDDVD_OK)
    {
        (void)hddvd_uri_disc_path(uri);
        (void)hddvd_uri_archive_name(uri);
        (void)hddvd_uri_member_name(uri);
        hddvd_uri_free(uri);
    }
    if (ctx && hddvd_open_resource(ctx, text, &stream) == HDDVD_OK)
    {
        unsigned char buffer[16];
        size_t got;
        (void)hddvd_stream_read_at(stream, 0, buffer, sizeof(buffer), &got);
        hddvd_stream_close(stream);
    }
    return 0;
}
