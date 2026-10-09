#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_xpl *xpl = NULL;
    size_t i;
    if (hddvd_xpl_open(data, size, &xpl) == HDDVD_OK)
    {
        hddvd_xpl_unmodeled record;
        for (i = 0; i < hddvd_xpl_unmodeled_count(xpl); i++)
            (void)hddvd_xpl_get_unmodeled(xpl, i, &record);
        for (i = 0; i < hddvd_xpl_title_count(xpl); i++)
        {
            hddvd_xpl_application_state state;
            hddvd_xpl_time at = {0, hddvd_xpl_frame_rate(xpl)};
            if (hddvd_xpl_application_count(xpl, i))
                (void)hddvd_xpl_get_application_state(xpl, i, 0, at, &state);
        }
    }
    hddvd_xpl_close(xpl);
    return 0;
}
