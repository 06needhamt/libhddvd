#include "hddvd.h"
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_object *object;
    if (hddvd_object_parse(data, size, &object) == HDDVD_OK)
        hddvd_object_free(object);
    return 0;
}
