#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 283u

static unsigned char *make_object(unsigned type, unsigned flags,
                                  const char *name, const void *payload,
                                  size_t payload_size, size_t trailer_size,
                                  size_t *object_size)
{
    unsigned char *object;
    assert(payload_size <= 0xffffffu);
    *object_size = HEADER_SIZE + payload_size + trailer_size;
    object = calloc(1, *object_size);
    assert(object);
    memcpy(object, "AACS", 4);
    object[4] = (unsigned char)type;
    object[5] = (unsigned char)(flags >> 16);
    object[6] = (unsigned char)(flags >> 8);
    object[7] = (unsigned char)flags;
    object[8] = (unsigned char)(payload_size >> 16);
    object[9] = (unsigned char)(payload_size >> 8);
    object[10] = (unsigned char)payload_size;
    if (name) memcpy(object + 11, name, strlen(name));
    memcpy(object + HEADER_SIZE, payload, payload_size);
    return object;
}

static int decrypt_copy(void *opaque, uint8_t type, const void *data, size_t size,
                        void **out, size_t *out_size)
{
    (void)opaque;
    assert(type == 1);
    *out = malloc(size);
    assert(*out);
    memcpy(*out, data, size);
    *out_size = size;
    return HDDVD_OK;
}

int main(void)
{
    static const char xml[] = "<Application/>";
    hddvd_object *parsed;
    unsigned char *object;
    void *payload;
    size_t object_size, payload_size;

    object = make_object(0x12, 0, "main.xmf.AACS", xml, sizeof(xml)-1, 4,
                         &object_size);
    assert(hddvd_object_parse(object, object_size, &parsed) == HDDVD_OK);
    assert(hddvd_object_type(parsed) == 0x12);
    assert(hddvd_object_flags(parsed) == 0);
    assert(!strcmp(hddvd_object_name(parsed), "main.xmf.AACS"));
    assert(hddvd_object_payload_size(parsed) == sizeof(xml)-1);
    assert(hddvd_object_trailer_size(parsed) == 4);
    assert(!hddvd_object_is_protected(parsed));
    assert(hddvd_object_read_payload(parsed, NULL, NULL, &payload, &payload_size) == HDDVD_OK);
    assert(payload_size == sizeof(xml)-1 && !memcmp(payload, xml, payload_size));
    free(payload);
    hddvd_object_free(parsed);
    {
        FILE *sink = tmpfile();
        assert(sink);
        assert(hddvd_xmf_dump(object, object_size, "wrapped.xmf", sink) == HDDVD_OK);
        fclose(sink);
    }
    object[8] = object[9] = object[10] = 0xff;
    assert(hddvd_object_parse(object, object_size, &parsed) == HDDVD_EPARSE);
    free(object);

    object = make_object(1, 0x100, NULL, "opaque", 6, 0, &object_size);
    assert(hddvd_object_parse(object, object_size, &parsed) == HDDVD_OK);
    assert(hddvd_object_is_protected(parsed));
    assert(hddvd_object_name(parsed) == NULL);
    assert(hddvd_object_read_payload(parsed, NULL, NULL, &payload, &payload_size) == HDDVD_EPROTECTED);
    assert(hddvd_object_read_payload(parsed, decrypt_copy, NULL, &payload, &payload_size) == HDDVD_OK);
    assert(payload_size == 6 && !memcmp(payload, "opaque", 6));
    free(payload);
    hddvd_object_free(parsed);
    free(object);
    assert(hddvd_object_parse("AACS", 4, &parsed) == HDDVD_EPARSE);
    return 0;
}
