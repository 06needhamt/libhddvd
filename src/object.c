#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define OBJECT_HEADER_SIZE 32u
#define OBJECT_PROTECTED 0x01u
#define AACS_HEADER_SIZE 283u
#define AACS_NAME_OFFSET 11u
#define AACS_TYPE_IMAGE 0x02u
#define AACS_TYPE_SCRIPT 0x11u
#define AACS_TYPE_TEXT 0x12u
static const unsigned char object_magic[8] = {'H', 'D', 'D', 'V', 'D', 'O', 'B', 'J'};
static const unsigned char aacs_magic[4] = {'A', 'A', 'C', 'S'};

struct hddvd_object
{
    hddvd_object_info info;
    hddvd_object_type type;
    char *name;
    unsigned char *payload;
    size_t payload_size;
    unsigned char *trailer;
    hddvd_object_format format;
    size_t name_offset;
    size_t name_size;
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

/* Validates HDDVDOBJ framing (docs/formats/aacs-object-envelope.md, synthetic
   layout) and fills the header-derived fields of a new object. */
static int parse_hddvdobj(const unsigned char *data, size_t size, hddvd_object *object)
{
    uint32_t header_size;
    uint16_t name_size, trailer_size;
    uint64_t payload_size;
    size_t i;
    if (size < OBJECT_HEADER_SIZE)
        return HDDVD_EPARSE;
    if (data[8] != 1 || (data[9] & (UINT8_MAX ^ OBJECT_PROTECTED)))
        return HDDVD_ENOTSUP;
    header_size = be32(data + 12);
    payload_size = be64(data + 16);
    name_size = be16(data + 24);
    trailer_size = be16(data + 26);
    if (header_size < OBJECT_HEADER_SIZE || name_size > header_size - OBJECT_HEADER_SIZE)
        return HDDVD_EPARSE;
    /* Compare each length with what remains, so a huge payload length cannot
       wrap the sum around to the input size. */
    if (header_size > size || payload_size > size - header_size ||
        trailer_size != size - header_size - payload_size)
        return HDDVD_EPARSE;
    if (name_size && memchr(data + OBJECT_HEADER_SIZE, 0, name_size))
        return HDDVD_EPARSE;
    for (i = OBJECT_HEADER_SIZE + name_size; i < header_size; i++)
        if (data[i])
            return HDDVD_EPARSE;
    object->format = HDDVD_OBJECT_FORMAT_HDDVDOBJ;
    object->info.version = data[8];
    object->info.flags = data[9];
    object->info.raw_type = be16(data + 10);
    object->info.unknown_1 = be32(data + 28);
    object->info.declared_length = size;
    object->info.payload_offset = header_size;
    object->info.payload_length = payload_size;
    object->info.trailer_offset = (uint64_t)header_size + payload_size;
    object->info.trailer_length = trailer_size;
    object->type = public_type(object->info.raw_type);
    object->name_offset = OBJECT_HEADER_SIZE;
    object->name_size = name_size;
    return HDDVD_OK;
}

/* Validates the observed AACS layout: a 283-byte header with the type at
   byte 4 and the big-endian payload length at byte 7, then the payload and a
   trailer. Only the clear types have a known name area and trailer size. */
static int parse_aacs(const unsigned char *data, size_t size, hddvd_object *object)
{
    const unsigned char *name_end;
    uint32_t payload_size;
    size_t trailer_size, i;
    uint8_t type;
    if (size < AACS_HEADER_SIZE)
        return HDDVD_EPARSE;
    type = data[4];
    payload_size = be32(data + 7);
    if (payload_size > size - AACS_HEADER_SIZE)
        return HDDVD_EPARSE;
    trailer_size = size - AACS_HEADER_SIZE - payload_size;
    if (trailer_size > UINT32_MAX)
        return HDDVD_ELIMIT;
    if (type == AACS_TYPE_IMAGE || type == AACS_TYPE_TEXT)
    {
        if (trailer_size != (type == AACS_TYPE_IMAGE ? 16u : 4u))
            return HDDVD_EPARSE;
        name_end = memchr(data + AACS_NAME_OFFSET, 0, AACS_HEADER_SIZE - AACS_NAME_OFFSET);
        if (!name_end || name_end == data + AACS_NAME_OFFSET)
            return HDDVD_EPARSE;
        for (i = AACS_NAME_OFFSET; data + i < name_end; i++)
            if (data[i] < 0x20 || data[i] > 0x7e)
                return HDDVD_EPARSE;
        for (i = (size_t)(name_end - data); i < AACS_HEADER_SIZE; i++)
            if (data[i])
                return HDDVD_EPARSE;
        object->name_offset = AACS_NAME_OFFSET;
        object->name_size = (size_t)(name_end - data) - AACS_NAME_OFFSET;
        object->type = type == AACS_TYPE_IMAGE ? HDDVD_OBJECT_IMAGE : HDDVD_OBJECT_TEXT;
    }
    else
    {
        object->info.flags = OBJECT_PROTECTED;
        object->type = type == AACS_TYPE_SCRIPT ? HDDVD_OBJECT_SCRIPT : HDDVD_OBJECT_UNKNOWN;
    }
    object->format = HDDVD_OBJECT_FORMAT_AACS;
    object->info.raw_type = type;
    object->info.unknown_1 = be16(data + 5);
    object->info.declared_length = size;
    object->info.payload_offset = AACS_HEADER_SIZE;
    object->info.payload_length = payload_size;
    object->info.trailer_offset = (uint64_t)AACS_HEADER_SIZE + payload_size;
    object->info.trailer_length = (uint32_t)trailer_size;
    return HDDVD_OK;
}

int hddvd_object_is_envelope(const void *input, size_t size)
{
    return input && ((size >= sizeof(object_magic) && !memcmp(input, object_magic, 8)) ||
                     (size >= sizeof(aacs_magic) && !memcmp(input, aacs_magic, 4)));
}

int hddvd_object_open_with_provider(const void *input, size_t size, hddvd_decrypt_fn decrypt,
                                    void *opaque, hddvd_object **result)
{
    const unsigned char *data = input;
    hddvd_object *object;
    int rc;
    if (!input || !result)
        return HDDVD_EINVAL;
    *result = NULL;
    if (!hddvd_object_is_envelope(input, size))
        return HDDVD_EPARSE;
    object = calloc(1, sizeof(*object));
    if (!object)
        return HDDVD_ENOMEM;
    rc = !memcmp(data, aacs_magic, sizeof(aacs_magic)) ? parse_aacs(data, size, object)
                                                       : parse_hddvdobj(data, size, object);
    if (!rc && object->name_size)
    {
        object->name = malloc(object->name_size + 1);
        if (!object->name)
            rc = HDDVD_ENOMEM;
        else
        {
            memcpy(object->name, data + object->name_offset, object->name_size);
            object->name[object->name_size] = 0;
        }
    }
    if (rc)
    {
        hddvd_object_close(object);
        return rc;
    }
    if (object->info.flags & OBJECT_PROTECTED)
    {
        void *clear = NULL;
        size_t clear_size = 0;
        if (!decrypt)
        {
            hddvd_object_close(object);
            return HDDVD_EPROTECTED;
        }
        rc = decrypt(opaque, &object->info, data + object->info.payload_offset,
                     (size_t)object->info.payload_length, &clear, &clear_size);
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
        object->payload_size = (size_t)object->info.payload_length;
        rc = copy_bytes(&object->payload, data + object->info.payload_offset, object->payload_size);
        if (rc)
        {
            hddvd_object_close(object);
            return rc;
        }
    }
    rc = copy_bytes(&object->trailer, data + object->info.trailer_offset,
                    object->info.trailer_length);
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
hddvd_object_format hddvd_object_get_format(const hddvd_object *object)
{
    return object ? object->format : HDDVD_OBJECT_FORMAT_NONE;
}
int hddvd_object_get_trailer(const hddvd_object *object, const void **data, size_t *size)
{
    if (!object || !data || !size)
        return HDDVD_EINVAL;
    *data = object->trailer;
    *size = object->info.trailer_length;
    return HDDVD_OK;
}

int hddvd_object_unwrap(const void *input, size_t input_size, hddvd_object_type expected,
                        unsigned char **owned, const void **data, size_t *size)
{
    hddvd_object *object = NULL;
    int rc;
    *owned = NULL;
    *data = input;
    *size = input_size;
    if (!hddvd_object_is_envelope(input, input_size))
        return HDDVD_OK;
    rc = hddvd_object_open(input, input_size, &object);
    if (rc)
        return rc;
    if (object->type != expected && object->type != HDDVD_OBJECT_TEXT)
        rc = HDDVD_EPARSE;
    else if (object->payload_size > (size_t)INT_MAX)
        rc = HDDVD_ELIMIT;
    else
    {
        /* Take ownership of the payload copy instead of copying it again. */
        *owned = object->payload;
        object->payload = NULL;
        *data = *owned;
        *size = object->payload_size;
    }
    hddvd_object_close(object);
    return rc;
}
