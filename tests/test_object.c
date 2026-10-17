#include "hddvd.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void put16(unsigned char *p, uint16_t n)
{
    p[0] = (unsigned char)(n >> 8);
    p[1] = (unsigned char)n;
}
static void put32(unsigned char *p, uint32_t n)
{
    p[0] = (unsigned char)(n >> 24);
    p[1] = (unsigned char)(n >> 16);
    p[2] = (unsigned char)(n >> 8);
    p[3] = (unsigned char)n;
}
static void put64(unsigned char *p, uint64_t n)
{
    put32(p, (uint32_t)(n >> 32));
    put32(p + 4, (uint32_t)n);
}

static size_t make(unsigned char *b, uint16_t type, unsigned flags)
{
    const char payload[] = "payload", name[] = "name";
    memset(b, 0, 64);
    memcpy(b, "HDDVDOBJ", 8);
    b[8] = 1;
    b[9] = (unsigned char)flags;
    put16(b + 10, type);
    put32(b + 12, 36);
    put64(b + 16, sizeof(payload) - 1);
    put16(b + 24, sizeof(name) - 1);
    put16(b + 26, 2);
    put32(b + 28, 0x12345678);
    memcpy(b + 32, name, sizeof(name) - 1);
    memcpy(b + 36, payload, sizeof(payload) - 1);
    b[43] = 0xaa;
    b[44] = 0xbb;
    return 45;
}

static int decrypt(void *opaque, const hddvd_object_info *info, const void *in, size_t size,
                   void **out, size_t *out_size)
{
    unsigned char *copy;
    (void)opaque;
    (void)info;
    copy = malloc(size);
    if (!copy)
        return HDDVD_ENOMEM;
    memcpy(copy, in, size);
    *out = copy;
    *out_size = size;
    return HDDVD_OK;
}

/* Original AACS-layout envelope: 283-byte header (type at 4, bytes 5-6,
   big-endian payload length at 7, name from 11), payload, then trailer. */
static size_t make_aacs(unsigned char *b, uint8_t type, const char *name, const char *payload,
                        size_t trailer)
{
    size_t length = strlen(payload), i;
    memset(b, 0, 283 + length + trailer);
    memcpy(b, "AACS", 4);
    b[4] = type;
    b[6] = 0x16;
    put32(b + 7, (uint32_t)length);
    if (name)
        memcpy(b + 11, name, strlen(name));
    else
        for (i = 11; i < 283; i++)
            b[i] = (unsigned char)(i * 7 + 1);
    memcpy(b + 283, payload, length);
    for (i = 0; i < trailer; i++)
        b[283 + length + i] = (unsigned char)(0xa0 + i);
    return 283 + length + trailer;
}

static int test_aacs(void)
{
    unsigned char bytes[400], corrupt[400];
    size_t size, got;
    hddvd_object *object = NULL;
    hddvd_object_info info;
    hddvd_stream *payload = NULL;
    const void *trailer;
    size_t trailer_size;
    char text[8];

    size = make_aacs(bytes, 0x12, "main.xmf.AACS", "<a/>", 4);
    if (!hddvd_object_is_envelope(bytes, size) || hddvd_object_is_envelope("AAC", 3) ||
        hddvd_object_open(bytes, size, &object) ||
        hddvd_object_get_format(object) != HDDVD_OBJECT_FORMAT_AACS ||
        hddvd_object_get_type(object) != HDDVD_OBJECT_TEXT ||
        strcmp(hddvd_object_name(object), "main.xmf.AACS") ||
        hddvd_object_get_info(object, &info) || info.raw_type != 0x12 || info.version != 0 ||
        info.unknown_1 != 0x16 || info.payload_offset != 283 || info.payload_length != 4 ||
        info.trailer_offset != 287 || info.flags != 0 ||
        hddvd_object_get_trailer(object, &trailer, &trailer_size) || trailer_size != 4 ||
        ((const unsigned char *)trailer)[3] != 0xa3 ||
        hddvd_object_open_payload(object, &payload) ||
        hddvd_stream_read_at(payload, 0, text, sizeof(text), &got) || got != 4 ||
        memcmp(text, "<a/>", 4))
        return 20;
    hddvd_stream_close(payload);
    hddvd_object_close(object);

    size = make_aacs(bytes, 0x02, "focus.png.AACS", "\x89PNG", 16);
    if (hddvd_object_open(bytes, size, &object) ||
        hddvd_object_get_type(object) != HDDVD_OBJECT_IMAGE)
        return 21;
    hddvd_object_close(object);

    /* Clear types have a known trailer size, name, and zero padding. */
    size = make_aacs(bytes, 0x12, "x.AACS", "<a/>", 5);
    if (hddvd_object_open(bytes, size, &object) != HDDVD_EPARSE)
        return 22;
    size = make_aacs(bytes, 0x02, "x.AACS", "\x89PNG", 4);
    if (hddvd_object_open(bytes, size, &object) != HDDVD_EPARSE)
        return 23;
    size = make_aacs(bytes, 0x12, "x.AACS", "<a/>", 4);
    memcpy(corrupt, bytes, size);
    corrupt[200] = 1;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE)
        return 24;
    memcpy(corrupt, bytes, size);
    corrupt[11] = 0;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE)
        return 25;
    memcpy(corrupt, bytes, size);
    corrupt[12] = '\n';
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE)
        return 26;
    memcpy(corrupt, bytes, size);
    put32(corrupt + 7, (uint32_t)size);
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE)
        return 27;
    if (hddvd_object_open(bytes, 282, &object) != HDDVD_EPARSE)
        return 28;

    /* Other types are opaque and protected, with any trailer size. */
    size = make_aacs(bytes, 0x11, NULL, "opaque!", 9);
    if (hddvd_object_open(bytes, size, &object) != HDDVD_EPROTECTED)
        return 29;
    if (hddvd_object_open_with_provider(bytes, size, decrypt, NULL, &object) ||
        hddvd_object_get_type(object) != HDDVD_OBJECT_SCRIPT || hddvd_object_name(object) ||
        hddvd_object_get_info(object, &info) || !(info.flags & 1) || info.trailer_length != 9)
        return 30;
    hddvd_object_close(object);
    size = make_aacs(bytes, 0x7f, NULL, "opaque!", 0);
    if (hddvd_object_open(bytes, size, &object) != HDDVD_EPROTECTED)
        return 31;
    return 0;
}

int main(void)
{
    unsigned char bytes[64], corrupt[64], data[7];
    size_t size = make(bytes, 1, 0), got;
    hddvd_object *object = NULL;
    hddvd_object_info info;
    hddvd_stream *payload = NULL;
    const void *trailer;
    size_t trailer_size;
    if (hddvd_object_open(bytes, size, &object) ||
        hddvd_object_get_type(object) != HDDVD_OBJECT_MANIFEST ||
        strcmp(hddvd_object_name(object), "name") || hddvd_object_get_info(object, &info) ||
        info.unknown_1 != 0x12345678 || info.payload_offset != 36 || info.payload_length != 7 ||
        hddvd_object_get_trailer(object, &trailer, &trailer_size) || trailer_size != 2 ||
        ((const unsigned char *)trailer)[1] != 0xbb ||
        hddvd_object_open_payload(object, &payload) ||
        hddvd_stream_read_at(payload, 0, data, sizeof(data), &got) || got != 7 ||
        memcmp(data, "payload", 7))
        return 1;
    hddvd_stream_close(payload);
    hddvd_object_close(object);
    memcpy(corrupt, bytes, size);
    corrupt[0] = 'X';
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE)
        return 2;
    memcpy(corrupt, bytes, size);
    put64(corrupt + 16, 8);
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE)
        return 3;
    /* A header longer than the input, with a payload length that wraps the
       64-bit sum of the lengths back to the input size (found by fuzz-object). */
    {
        unsigned char *exact = malloc(size);
        int rc;
        if (!exact)
            return 46;
        memcpy(exact, bytes, 36);
        memset(exact + 36, 0, size - 36);
        put32(exact + 12, 200);
        put64(exact + 16, (uint64_t)size - 202u);
        rc = hddvd_object_open(exact, size, &object);
        free(exact);
        if (rc != HDDVD_EPARSE)
            return 46;
    }
    memcpy(corrupt, bytes, size);
    corrupt[8] = 2;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_ENOTSUP)
        return 7;
    memcpy(corrupt, bytes, size);
    corrupt[9] = 0x80;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_ENOTSUP)
        return 8;
    memcpy(corrupt, bytes, size);
    put32(corrupt + 12, 31);
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE)
        return 9;
    memcpy(corrupt, bytes, size);
    corrupt[32] = 0;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE)
        return 10;
    memcpy(corrupt, bytes, size);
    put16(corrupt + 24, 3);
    corrupt[35] = 1;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE)
        return 11;
    memcpy(corrupt, bytes, size);
    corrupt[9] = 1;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPROTECTED)
        return 4;
    if (hddvd_object_open_with_provider(corrupt, size, decrypt, NULL, &object) ||
        hddvd_object_get_type(object) != HDDVD_OBJECT_MANIFEST)
        return 5;
    hddvd_object_close(object);
    if (hddvd_object_open_unwrapped(HDDVD_OBJECT_MARKUP, "x", 1, &object))
        return 6;
    hddvd_object_close(object);
    if (hddvd_object_get_format(NULL) != HDDVD_OBJECT_FORMAT_NONE)
        return 12;
    return test_aacs();
}
