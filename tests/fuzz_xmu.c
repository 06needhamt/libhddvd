#include "hddvd.h"
#include <stdint.h>
#include <stdio.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FILE *sink = tmpfile();
    if (sink) {
        (void)hddvd_xmu_dump(data, size, "fuzz.xmu", sink);
        fclose(sink);
    }
    return 0;
}
