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
    if (!copy) {
        return HDDVD_ENOMEM;
    }
    memcpy(copy, in, size);
    *out = copy;
    *out_size = size;
    return HDDVD_OK;
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
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE) {
        return 2;
    }
    memcpy(corrupt, bytes, size);
    put64(corrupt + 16, 8);
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE) {
        return 3;
    }
    memcpy(corrupt, bytes, size);
    corrupt[8] = 2;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_ENOTSUP) {
        return 7;
    }
    memcpy(corrupt, bytes, size);
    corrupt[9] = 0x80;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_ENOTSUP) {
        return 8;
    }
    memcpy(corrupt, bytes, size);
    put32(corrupt + 12, 31);
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE) {
        return 9;
    }
    memcpy(corrupt, bytes, size);
    corrupt[32] = 0;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE) {
        return 10;
    }
    memcpy(corrupt, bytes, size);
    put16(corrupt + 24, 3);
    corrupt[35] = 1;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPARSE) {
        return 11;
    }
    memcpy(corrupt, bytes, size);
    corrupt[9] = 1;
    if (hddvd_object_open(corrupt, size, &object) != HDDVD_EPROTECTED) {
        return 4;
    }
    if (hddvd_object_open_with_provider(corrupt, size, decrypt, NULL, &object) ||
        hddvd_object_get_type(object) != HDDVD_OBJECT_MANIFEST)
        return 5;
    hddvd_object_close(object);
    if (hddvd_object_open_unwrapped(HDDVD_OBJECT_MARKUP, "x", 1, &object)) {
        return 6;
    }
    hddvd_object_close(object);
    return 0;
}
