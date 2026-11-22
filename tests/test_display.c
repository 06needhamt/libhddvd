/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct decode_state
{
    int calls;
    int releases;
} decode_state;
static void release_plane(void *opaque, const uint8_t *pixels)
{
    decode_state *s = opaque;
    s->releases++;
    free((void *)pixels);
}
static int decode(void *opaque, const char *id, const void *data, size_t size, hddvd_image_plane *p)
{
    decode_state *s = opaque;
    uint8_t *pixels;
    (void)id;
    (void)data;
    (void)size;
    s->calls++;
    pixels = (uint8_t *)calloc(1, 4U * 20U * 10U);
    if (!pixels)
        return HDDVD_ENOMEM;
    p->pixels = pixels;
    p->width = 20;
    p->height = 10;
    p->stride = 80;
    p->release = release_plane;
    p->release_opaque = s;
    return HDDVD_OK;
}
static int bad_decode(void *opaque, const char *id, const void *data, size_t size,
                      hddvd_image_plane *p)
{
    (void)opaque;
    (void)id;
    (void)data;
    (void)size;
    memset(p, 0, sizeof(*p));
    p->pixels = (const uint8_t *)"x";
    p->width = UINT32_MAX;
    p->height = 1;
    p->stride = 1;
    return HDDVD_OK;
}

int main(void)
{
    hddvd_image_cache *cache = NULL, *bad = NULL;
    hddvd_display_list *a = NULL, *b = NULL;
    hddvd_display_limits limits;
    decode_state state = {0, 0};
    hddvd_display_quad q;
    hddvd_video_transform t;
    int32_t x, y;
    uint64_t id;
    static const uint8_t encoded[] = {1};
    hddvd_scene_node nodes[3] = {{1,
                                  SIZE_MAX,
                                  {100, 50, 200, 100},
                                  {0, 0, 150, 80},
                                  0,
                                  255,
                                  1,
                                  0,
                                  0,
                                  {NULL, NULL, 0},
                                  {NULL, NULL, 0}},
                                 {2,
                                  0,
                                  {25, 10, 50, 40},
                                  {0, 0, 0, 0},
                                  4,
                                  255,
                                  1,
                                  0,
                                  1,
                                  {"normal", encoded, 1},
                                  {"focus", encoded, 1}},
                                 {3,
                                  0,
                                  {30, 15, 50, 40},
                                  {0, 0, 0, 0},
                                  4,
                                  200,
                                  1,
                                  1,
                                  1,
                                  {"normal2", encoded, 1},
                                  {"focus", encoded, 1}}};
    hddvd_display_limits_default(&limits);
    limits.max_images = 4;
    if (hddvd_image_cache_create(decode, &state, &limits, &cache) != HDDVD_OK)
        return 1;
    if (hddvd_display_build(cache, 1920, 1080, nodes, 3, NULL, &a) != HDDVD_OK)
        return 2;
    if (hddvd_display_quad_count(a) != 2 || state.calls != 2 || hddvd_display_damage_count(a) != 2)
        return 3;
    if (hddvd_display_get_quad(a, 0, &q) != HDDVD_OK || q.node_id != 2 || q.destination.x != 125 ||
        q.destination.y != 60 || q.clip.width != 50)
        return 4;
    /* Reverse paint order selects node 3; both focused nodes share one cached image. */
    if (hddvd_display_hit_test(a, 135, 70, &id) != HDDVD_OK || id != 3)
        return 5;
    nodes[1].focused = 1;
    if (hddvd_display_build(cache, 1920, 1080, nodes, 3, a, &b) != HDDVD_OK || state.calls != 2)
        return 6;
    if (hddvd_display_damage_count(b) != 2)
        return 7;
    hddvd_video_transform_fit(1920, 1080, 1024, 768, &t);
    if (t.width != 1024 || t.height != 576 || t.y != 96 ||
        hddvd_video_to_logical(&t, 512, 384, &x, &y) != HDDVD_OK || x != 960 || y != 540)
        return 8;
    if (hddvd_video_to_logical(&t, 0, 0, &x, &y) != HDDVD_ENOTFOUND)
        return 9;
    hddvd_display_list_close(b);
    hddvd_display_list_close(a);
    hddvd_image_cache_destroy(cache);
    if (state.releases != 2)
        return 10;
    limits.max_dimension = 32;
    if (hddvd_image_cache_create(bad_decode, NULL, &limits, &bad) != HDDVD_OK)
        return 11;
    if (hddvd_display_build(bad, 1920, 1080, nodes + 1, 1, NULL, &a) != HDDVD_EINVAL)
        return 12; /* invalid parent */
    nodes[1].parent = SIZE_MAX;
    if (hddvd_display_build(bad, 1920, 1080, nodes + 1, 1, NULL, &a) != HDDVD_EPARSE)
        return 13;
    hddvd_image_cache_destroy(bad);
    return 0;
}
