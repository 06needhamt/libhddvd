#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_script *script = NULL;
    if (hddvd_script_open(data, size, &script) == HDDVD_OK)
        hddvd_script_close(script);
    return 0;
}
