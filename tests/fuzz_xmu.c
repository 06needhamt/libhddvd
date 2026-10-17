#include "hddvd.h"
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    size_t i;
    (void)hddvd_xmu_open(data, size, &xmu);
    if (xmu)
        (void)hddvd_xmu_styles_open(xmu, &styles);
    if (styles)
    {
        hddvd_xmu_style_unmodeled unmodeled;
        for (i = 0; i < hddvd_xmu_node_count(xmu); i++)
        {
            hddvd_xmu_style style;
            hddvd_xmu_style_details details;
            (void)hddvd_xmu_get_computed_style(styles, i, &style);
            (void)hddvd_xmu_get_style_details(styles, i, 0, &details);
            (void)hddvd_xmu_get_style_details(styles, i, 1, &details);
            (void)hddvd_xmu_style_image_for_state(styles, i, 1);
        }
        for (i = 0; i < hddvd_xmu_styles_unmodeled_count(styles); i++)
            (void)hddvd_xmu_styles_get_unmodeled(styles, i, &unmodeled);
    }
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
    return 0;
}
