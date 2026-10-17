#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_xpl *xpl = NULL;
    size_t i, j, k;
    if (hddvd_xpl_open(data, size, &xpl) == HDDVD_OK)
    {
        hddvd_xpl_unmodeled record;
        (void)hddvd_xpl_description(xpl);
        for (i = 0; i < hddvd_xpl_unmodeled_count(xpl); i++)
            (void)hddvd_xpl_get_unmodeled(xpl, i, &record);
        for (i = 0; i < hddvd_xpl_title_count(xpl); i++)
        {
            hddvd_xpl_application_state state;
            hddvd_xpl_time at = {0, hddvd_xpl_frame_rate(xpl)};
            hddvd_xpl_title_details title;
            hddvd_xpl_track_details track;
            const char *description;
            (void)hddvd_xpl_get_title_details(xpl, i, &title);
            for (j = 0; j < hddvd_xpl_clip_count(xpl, i); j++)
                (void)hddvd_xpl_get_clip_description(xpl, i, j, &description);
            for (j = 0; j < hddvd_xpl_track_count(xpl, i); j++)
                (void)hddvd_xpl_get_track_details(xpl, i, j, &track);
            if (hddvd_xpl_application_count(xpl, i))
                (void)hddvd_xpl_get_application_state(xpl, i, 0, at, &state);
        }
        /* Title applications, then playlist-wide ones (SIZE_MAX). */
        for (i = 0; i <= hddvd_xpl_title_count(xpl); i++)
        {
            size_t title = i < hddvd_xpl_title_count(xpl) ? i : SIZE_MAX;
            for (j = 0; j < hddvd_xpl_application_count(xpl, title); j++)
            {
                hddvd_xpl_application_details application;
                (void)hddvd_xpl_get_application_details(xpl, title, j, &application);
                for (k = 0; k < hddvd_xpl_application_resource_count(xpl, title, j); k++)
                {
                    hddvd_xpl_resource_details resource;
                    (void)hddvd_xpl_get_application_resource_details(xpl, title, j, k, &resource);
                }
            }
        }
    }
    hddvd_xpl_close(xpl);
    return 0;
}
