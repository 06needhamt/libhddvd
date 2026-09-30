#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define OBJECT_HEADER_SIZE 283u
#define OBJECT_PROTECTED_FLAG 0x000100u

struct hddvd_object {
    unsigned char *bytes;
    uint32_t flags;
    size_t payload_size;
    size_t trailer_size;
    uint8_t type;
    char *name;
};

static uint32_t be24(const unsigned char *data)
{
    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

int hddvd_object_parse(const void *data, size_t size, hddvd_object **out)
{
    const unsigned char *bytes = data;
    hddvd_object *object;
    size_t name_length = 0;

    if (!data || !out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (size < OBJECT_HEADER_SIZE || memcmp(bytes, "AACS", 4))
        return HDDVD_EPARSE;
    object = calloc(1, sizeof(*object));
    if (!object)
        return HDDVD_ENOMEM;
    object->type = bytes[4];
    object->flags = be24(bytes + 5);
    object->payload_size = be24(bytes + 8);
    if (object->payload_size > size - OBJECT_HEADER_SIZE) {
        free(object);
        return HDDVD_EPARSE;
    }
    object->trailer_size = size - OBJECT_HEADER_SIZE - object->payload_size;
    object->bytes = malloc(size);
    if (!object->bytes) {
        free(object);
        return HDDVD_ENOMEM;
    }
    memcpy(object->bytes, data, size);
    if (!(object->flags & OBJECT_PROTECTED_FLAG)) {
        while (11 + name_length < OBJECT_HEADER_SIZE && bytes[11 + name_length])
            name_length++;
        if (11 + name_length == OBJECT_HEADER_SIZE) {
            hddvd_object_free(object);
            return HDDVD_EPARSE;
        }
        if (name_length) {
            object->name = malloc(name_length + 1);
            if (!object->name) {
                hddvd_object_free(object);
                return HDDVD_ENOMEM;
            }
            memcpy(object->name, bytes + 11, name_length);
            object->name[name_length] = 0;
        }
    }
    *out = object;
    return HDDVD_OK;
}

void hddvd_object_free(hddvd_object *object)
{
    if (object) {
        free(object->name);
        free(object->bytes);
        free(object);
    }
}

uint8_t hddvd_object_type(const hddvd_object *object){return object?object->type:0;}
uint32_t hddvd_object_flags(const hddvd_object *object){return object?object->flags:0;}
const char *hddvd_object_name(const hddvd_object *object){return object?object->name:NULL;}
size_t hddvd_object_payload_size(const hddvd_object *object){return object?object->payload_size:0;}
size_t hddvd_object_trailer_size(const hddvd_object *object){return object?object->trailer_size:0;}
int hddvd_object_is_protected(const hddvd_object *object){return object&&(object->flags&OBJECT_PROTECTED_FLAG)!=0;}

int hddvd_object_read_payload(const hddvd_object *object, hddvd_decrypt_fn decrypt,
                              void *opaque, void **out, size_t *size)
{
    const unsigned char *payload;
    void *copy;
    if (!object || !out || !size)
        return HDDVD_EINVAL;
    *out = NULL;
    *size = 0;
    payload = object->bytes + OBJECT_HEADER_SIZE;
    if (hddvd_object_is_protected(object)) {
        int rc;
        if (!decrypt)
            return HDDVD_EPROTECTED;
        rc = decrypt(opaque, object->type, payload, object->payload_size,
                     out, size);
        if (!rc && !*out)
            return HDDVD_EIO;
        return rc;
    }
    copy = malloc(object->payload_size ? object->payload_size : 1);
    if (!copy)
        return HDDVD_ENOMEM;
    if (object->payload_size)
        memcpy(copy, payload, object->payload_size);
    *out = copy;
    *size = object->payload_size;
    return HDDVD_OK;
}

int hddvd_object_unwrap_xml(const void *data, size_t size, const void **xml,
                            size_t *xml_size, hddvd_object **owner)
{
    hddvd_object *object;
    int rc;
    *owner = NULL;
    *xml = data;
    *xml_size = size;
    if (size < 4 || memcmp(data, "AACS", 4))
        return HDDVD_OK;
    rc = hddvd_object_parse(data, size, &object);
    if (rc)
        return rc;
    if (hddvd_object_is_protected(object)) {
        hddvd_object_free(object);
        return HDDVD_EPROTECTED;
    }
    *xml = object->bytes + OBJECT_HEADER_SIZE;
    *xml_size = object->payload_size;
    *owner = object;
    return HDDVD_OK;
}
