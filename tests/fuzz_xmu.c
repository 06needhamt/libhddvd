#include "hddvd.h"
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    (void)hddvd_xmu_open(data, size, &xmu);
    if (xmu)
        (void)hddvd_xmu_styles_open(xmu, &styles);
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
    return 0;
}
