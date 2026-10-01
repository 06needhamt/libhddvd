#include "hddvd.h"
#include <stdint.h>
#include <stdio.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FILE *sink = tmpfile();
    if (sink)
    {
        (void)hddvd_xmf_dump(data, size, "fuzz.xmf", sink);
        fclose(sink);
    }
    return 0;
}
