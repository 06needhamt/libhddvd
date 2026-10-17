#include "hddvd.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Supplies bytes for some image URLs and reports others as missing. */
static int image(void *opaque, const char *url, const void **data, size_t *size)
{
    static const unsigned char bytes[] = {0};
    (void)opaque;
    if (strlen(url) % 2)
        return HDDVD_ENOTFOUND;
    *data = bytes;
    *size = sizeof(bytes);
    return HDDVD_OK;
}

static void release(void *opaque, const uint8_t *pixels)
{
    (void)opaque;
    free((void *)pixels);
}

/* Image sizes follow the URL length, so placement sees varied sizes. */
static int decode(void *opaque, const char *id, const void *data, size_t size,
                  hddvd_image_plane *plane)
{
    size_t length = strlen(id);
    (void)opaque;
    (void)data;
    (void)size;
    memset(plane, 0, sizeof(*plane));
    plane->width = (uint32_t)(1 + length % 17);
    plane->height = (uint32_t)(1 + length % 5);
    plane->stride = plane->width * 4u;
    plane->pixels = calloc(plane->height, plane->stride);
    plane->release = release;
    return plane->pixels ? HDDVD_OK : HDDVD_ENOMEM;
}

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
        {
            hddvd_scene *scene = NULL;
            hddvd_image_cache *cache = NULL;
            hddvd_display_list *list = NULL;
            if (hddvd_scene_build(xmu, styles, size % 8, image, NULL, &scene) == HDDVD_OK &&
                hddvd_image_cache_create(decode, NULL, NULL, &cache) == HDDVD_OK &&
                hddvd_display_build_scene(cache, 1920, 1080, scene, NULL, &list) == HDDVD_OK)
            {
                hddvd_display_item item;
                uint64_t hit;
                for (i = 0; i < hddvd_display_item_count(list); i++)
                    (void)hddvd_display_get_item(list, i, &item);
                (void)hddvd_display_hit_test(list, 960, 540, &hit);
            }
            hddvd_display_list_close(list);
            hddvd_image_cache_destroy(cache);
            hddvd_scene_close(scene);
        }
    }
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
    return 0;
}
