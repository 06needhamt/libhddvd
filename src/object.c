#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define OBJECT_HEADER_SIZE 32u
#define OBJECT_PROTECTED 0x01u
static const unsigned char object_magic[8] = {'H', 'D', 'D', 'V', 'D', 'O', 'B', 'J'};

struct hddvd_object
{
    hddvd_object_info info;
    hddvd_object_type type;
    char *name;
    unsigned char *payload;
    size_t payload_size;
    unsigned char *trailer;
};

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const unsigned char *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static hddvd_object_type public_type(uint16_t type)
{
    return type <= (uint16_t)HDDVD_OBJECT_IMAGE ? (hddvd_object_type)type : HDDVD_OBJECT_UNKNOWN;
}

void hddvd_object_close(hddvd_object *object)
{
    if (!object)
        return;
    free(object->name);
    free(object->payload);
    free(object->trailer);
    free(object);
}

static int copy_bytes(unsigned char **out, const unsigned char *source, size_t size)
{
    if (!size)
    {
        *out = NULL;
        return HDDVD_OK;
    }
    *out = malloc(size);
    if (!*out)
        return HDDVD_ENOMEM;
    memcpy(*out, source, size);
    return HDDVD_OK;
}

int hddvd_object_open_with_provider(const void *input, size_t size, hddvd_decrypt_fn decrypt,
                                    void *opaque, hddvd_object **result)
{
    const unsigned char *data = input;
    hddvd_object *object;
    uint32_t header_size;
    uint16_t name_size, trailer_size;
    uint64_t payload_size, total;
    size_t i;
    int rc;
    if (!input || !result)
        return HDDVD_EINVAL;
    *result = NULL;
    if (size < OBJECT_HEADER_SIZE || memcmp(data, object_magic, sizeof(object_magic)))
        return HDDVD_EPARSE;
    if (data[8] != 1 || (data[9] & (UINT8_MAX ^ OBJECT_PROTECTED)))
        return HDDVD_ENOTSUP;
    header_size = be32(data + 12);
    payload_size = be64(data + 16);
    name_size = be16(data + 24);
    trailer_size = be16(data + 26);
    if (header_size < OBJECT_HEADER_SIZE || name_size > header_size - OBJECT_HEADER_SIZE)
        return HDDVD_EPARSE;
    total = (uint64_t)header_size + payload_size + trailer_size;
    if (total != size || payload_size > SIZE_MAX)
        return HDDVD_EPARSE;
    if (name_size && memchr(data + OBJECT_HEADER_SIZE, 0, name_size))
        return HDDVD_EPARSE;
    for (i = OBJECT_HEADER_SIZE + name_size; i < header_size; i++)
        if (data[i])
            return HDDVD_EPARSE;
    object = calloc(1, sizeof(*object));
    if (!object)
        return HDDVD_ENOMEM;
    object->info.version = data[8];
    object->info.flags = data[9];
    object->info.raw_type = be16(data + 10);
    object->info.unknown_1 = be32(data + 28);
    object->info.declared_length = total;
    object->info.payload_offset = header_size;
    object->info.payload_length = payload_size;
    object->info.trailer_offset = (uint64_t)header_size + payload_size;
    object->info.trailer_length = trailer_size;
    object->type = public_type(object->info.raw_type);
    if (name_size)
    {
        object->name = malloc((size_t)name_size + 1);
        if (!object->name)
        {
            hddvd_object_close(object);
            return HDDVD_ENOMEM;
        }
        memcpy(object->name, data + OBJECT_HEADER_SIZE, name_size);
        object->name[name_size] = 0;
    }
    if (data[9] & OBJECT_PROTECTED)
    {
        void *clear = NULL;
        size_t clear_size = 0;
        if (!decrypt)
        {
            hddvd_object_close(object);
            return HDDVD_EPROTECTED;
        }
        rc = decrypt(opaque, &object->info, data + header_size, (size_t)payload_size, &clear,
                     &clear_size);
        if (rc || (!clear && clear_size))
        {
            free(clear);
            hddvd_object_close(object);
            return rc ? rc : HDDVD_EINVAL;
        }
        object->payload = clear;
        object->payload_size = clear_size;
    }
    else
    {
        object->payload_size = (size_t)payload_size;
        rc = copy_bytes(&object->payload, data + header_size, object->payload_size);
        if (rc)
        {
            hddvd_object_close(object);
            return rc;
        }
    }
    rc = copy_bytes(&object->trailer, data + object->info.trailer_offset, trailer_size);
    if (rc)
    {
        hddvd_object_close(object);
        return rc;
    }
    *result = object;
    return HDDVD_OK;
}

int hddvd_object_open(const void *input, size_t size, hddvd_object **result)
{
    return hddvd_object_open_with_provider(input, size, NULL, NULL, result);
}

int hddvd_object_open_unwrapped(hddvd_object_type type, const void *input, size_t size,
                                hddvd_object **result)
{
    hddvd_object *object;
    int rc;
    if ((!input && size) || !result || type == HDDVD_OBJECT_UNKNOWN)
        return HDDVD_EINVAL;
    *result = NULL;
    object = calloc(1, sizeof(*object));
    if (!object)
        return HDDVD_ENOMEM;
    object->type = type;
    object->info.raw_type = (uint16_t)type;
    object->info.payload_length = size;
    object->info.declared_length = size;
    object->payload_size = size;
    rc = copy_bytes(&object->payload, input, size);
    if (rc)
    {
        hddvd_object_close(object);
        return rc;
    }
    *result = object;
    return HDDVD_OK;
}

int hddvd_object_open_stream(hddvd_stream *stream, hddvd_object **result)
{
    uint64_t length;
    unsigned char *data;
    size_t got = 0;
    int rc;
    if (!stream || !result)
        return HDDVD_EINVAL;
    length = hddvd_stream_size(stream);
    if (length > SIZE_MAX)
        return HDDVD_ELIMIT;
    data = length ? malloc((size_t)length) : NULL;
    if (length && !data)
        return HDDVD_ENOMEM;
    rc = hddvd_stream_read_at(stream, 0, data, (size_t)length, &got);
    if (!rc && got != length)
        rc = HDDVD_EIO;
    if (!rc)
        rc = hddvd_object_open(data, (size_t)length, result);
    free(data);
    return rc;
}

int hddvd_object_get_info(const hddvd_object *object, hddvd_object_info *info)
{
    if (!object || !info)
        return HDDVD_EINVAL;
    *info = object->info;
    return HDDVD_OK;
}
hddvd_object_type hddvd_object_get_type(const hddvd_object *object)
{
    return object ? object->type : HDDVD_OBJECT_UNKNOWN;
}
const char *hddvd_object_name(const hddvd_object *object)
{
    return object ? object->name : NULL;
}
int hddvd_object_open_payload(const hddvd_object *object, hddvd_stream **stream)
{
    return object && stream
               ? hddvd_stream_open_memory(object->payload, object->payload_size, stream)
               : HDDVD_EINVAL;
}
int hddvd_object_get_trailer(const hddvd_object *object, const void **data, size_t *size)
{
    if (!object || !data || !size)
        return HDDVD_EINVAL;
    *data = object->trailer;
    *size = object->info.trailer_length;
    return HDDVD_OK;
}
