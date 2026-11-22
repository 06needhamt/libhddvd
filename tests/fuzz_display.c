/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The input configures four images, a viewport and up to 64 scene nodes.
   Header (16 bytes): viewport width/height (2 x 16 bits), per-image width and
   height bytes (4 x 2), a flag byte (bit n gives image n a too-short stride),
   and three bytes for the video transform. Each node is 20 bytes. */
#define HEADER 16u
#define NODE 20u
#define MAX_NODES 64u

static void release(void *opaque, const uint8_t *pixels)
{
    (void)opaque;
    free((void *)pixels);
}

static int decode(void *opaque, const char *id, const void *data, size_t size,
                  hddvd_image_plane *plane)
{
    const uint8_t *spec = data;
    uint8_t *pixels;
    size_t stride, bytes;
    (void)opaque;
    (void)id;
    if (size < 3)
        return HDDVD_EPARSE;
    memset(plane, 0, sizeof(*plane));
    plane->width = spec[0] % 33u;
    plane->height = spec[1] % 33u;
    stride = (size_t)plane->width * 4u;
    /* A lying decoder: rows shorter than the claimed width. */
    if (spec[2] && stride)
        stride--;
    bytes = plane->height ? stride * plane->height : 1;
    pixels = calloc(1, bytes ? bytes : 1);
    if (!pixels)
        return HDDVD_ENOMEM;
    plane->pixels = pixels;
    plane->stride = stride;
    plane->release = release;
    return HDDVD_OK;
}

static int32_t s16(const uint8_t *b)
{
    return (int16_t)(uint16_t)(b[0] << 8 | b[1]);
}

static uint32_t u16(const uint8_t *b)
{
    return (uint32_t)(b[0] << 8 | b[1]);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const char *const names[] = {"i0", "i1", "i2", "i3"};
    uint8_t specs[4][3];
    hddvd_scene_node nodes[MAX_NODES];
    hddvd_display_limits limits;
    hddvd_image_cache *cache = NULL;
    hddvd_display_list *first = NULL, *second = NULL;
    hddvd_video_transform transform;
    size_t i, count;
    uint32_t width, height;
    uint64_t hit;
    int32_t x, y;
    if (size < HEADER)
        return 0;
    width = u16(data);
    height = u16(data + 2);
    for (i = 0; i < 4; i++)
    {
        specs[i][0] = data[4 + 2 * i];
        specs[i][1] = data[5 + 2 * i];
        specs[i][2] = (uint8_t)((data[12] >> i) & 1u);
    }
    hddvd_video_transform_fit(width, height, 1u + data[13] * 8u, 1u + data[14] * 8u, &transform);
    (void)hddvd_video_to_logical(&transform, data[15], data[13], &x, &y);

    count = (size - HEADER) / NODE;
    if (count > MAX_NODES)
        count = MAX_NODES;
    memset(nodes, 0, sizeof(nodes));
    for (i = 0; i < count; i++)
    {
        const uint8_t *b = data + HEADER + i * NODE;
        hddvd_scene_node *n = &nodes[i];
        n->id = (uint64_t)i + 1u;
        /* Mostly valid parents, sometimes a root, sometimes a forward or
           self reference that must be rejected. */
        n->parent = b[0] == 0xff ? i : (b[0] % (i + 1u) == i ? SIZE_MAX : b[0] % (i + 1u));
        n->bounds.x = s16(b + 1);
        n->bounds.y = s16(b + 3);
        n->bounds.width = u16(b + 5);
        n->bounds.height = u16(b + 7);
        n->clip.x = (int8_t)b[9];
        n->clip.y = (int8_t)b[10];
        n->clip.width = b[11];
        n->clip.height = b[12];
        n->z_index = (int8_t)b[13];
        n->opacity = b[14];
        n->visible = (uint8_t)(b[15] & 1u);
        n->focused = (uint8_t)((b[15] >> 1) & 1u);
        n->hit_test = (uint8_t)((b[15] >> 2) & 1u);
        if (b[16] & 4u)
        {
            n->normal_image.identity = names[b[16] & 3u];
            n->normal_image.encoded = specs[b[16] & 3u];
            n->normal_image.encoded_size = 3;
        }
        if (b[17] & 4u)
        {
            n->focused_image.identity = names[b[17] & 3u];
            n->focused_image.encoded = specs[b[17] & 3u];
            n->focused_image.encoded_size = 3;
        }
    }

    hddvd_display_limits_default(&limits);
    limits.max_images = 3;
    limits.max_quads = 32;
    if (hddvd_image_cache_create(decode, NULL, &limits, &cache) != HDDVD_OK)
        return 0;
    if (hddvd_display_build(cache, width, height, nodes, count, NULL, &first) == HDDVD_OK)
    {
        hddvd_display_quad quad;
        hddvd_rect damage;
        for (i = 0; i < hddvd_display_quad_count(first); i++)
            (void)hddvd_display_get_quad(first, i, &quad);
        (void)hddvd_display_hit_test(first, x, y, &hit);
        /* Move focus and rebuild against the first list to exercise damage. */
        for (i = 0; i < count; i++)
            nodes[i].focused = (uint8_t)!nodes[i].focused;
        if (hddvd_display_build(cache, width, height, nodes, count, first, &second) == HDDVD_OK)
        {
            for (i = 0; i < hddvd_display_damage_count(second); i++)
                (void)hddvd_display_get_damage(second, i, &damage);
            (void)hddvd_display_hit_test(second, (int32_t)width / 2, (int32_t)height / 2, &hit);
            hddvd_display_list_close(second);
        }
        hddvd_display_list_close(first);
    }
    hddvd_image_cache_destroy(cache);
    return 0;
}
