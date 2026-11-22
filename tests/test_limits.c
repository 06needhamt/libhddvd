/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Proves that every hddvd_config_limits field with an implemented consumer is
   enforced through the unified *_with_config entry points: defaults accept a
   small valid input and lowering only that field rejects it. max_resource_bytes
   is covered by test_resource.c. The script heap and recursion limits have no
   consumer until a script runtime exists. */

static unsigned char *read_fixture(const char *name, size_t *size)
{
    char path[1024];
    FILE *file;
    long end;
    unsigned char *bytes;
    snprintf(path, sizeof(path), "%s/%s", HDDVD_FIXTURE_DIR, name);
    file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    end = ftell(file);
    assert(end > 0 && fseek(file, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)end);
    assert(bytes && fread(bytes, 1, (size_t)end, file) == (size_t)end);
    fclose(file);
    *size = (size_t)end;
    return bytes;
}

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}
static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}
static uint32_t crc32(const unsigned char *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    size_t i;
    for (i = 0; i < n; i++)
    {
        unsigned k;
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ ((0u - (c & 1u)) & 0xedb88320u);
    }
    return ~c;
}

static void test_invalid_config(void)
{
    hddvd_config_limits config;
    hddvd_nav *nav = NULL;
    hddvd_config_limits_default(&config);
    config.max_queued_events = 0;
    assert(hddvd_nav_create_with_config(&config, NULL, NULL, NULL, &nav) == HDDVD_EINVAL);
    hddvd_config_limits_default(&config);
    config.max_script_recursion = 0;
    assert(hddvd_nav_create_with_config(&config, NULL, NULL, NULL, &nav) == HDDVD_EINVAL);
    assert(hddvd_nav_create_with_config(NULL, NULL, NULL, NULL, &nav) == HDDVD_EINVAL);
    assert(nav == NULL);
}

/* Two 5-byte members named a.xmf and b.xmf. */
static size_t build_aca(unsigned char *out)
{
    static const char *names[2] = {"a.xmf", "b.xmf"};
    static const unsigned char data[2][5] = {{'<', 'a', '/', '>', '\n'},
                                             {'<', 'b', '/', '>', '\n'}};
    size_t pos = 32, first = 32 + 2 * (46 + 5), i;
    memset(out, 0, first + 10);
    memcpy(out, "HDDVDACA", 8);
    put16(out + 8, 0x10);
    put16(out + 10, 1);
    put16(out + 12, 2);
    put16(out + 14, 0x35);
    for (i = 0; i < 2; i++)
    {
        put32(out + pos, (uint32_t)(first + 5 * i));
        put32(out + pos + 4, 5);
        put32(out + pos + 8, crc32(data[i], 5));
        out[pos + 12] = 0xff;
        out[pos + 13] = 5;
        memcpy(out + pos + 14, names[i], 5);
        pos += 46 + 5;
        memcpy(out + first + 5 * i, data[i], 5);
    }
    return first + 10;
}

static int open_aca(const unsigned char *bytes, size_t size, const hddvd_config_limits *config)
{
    hddvd_stream *stream = NULL;
    hddvd_aca *aca = NULL;
    int rc;
    assert(hddvd_stream_open_memory(bytes, size, &stream) == HDDVD_OK);
    rc = hddvd_aca_open_stream_with_config(stream, config, &aca);
    assert((rc == HDDVD_OK) == (aca != NULL));
    if (aca)
        assert(hddvd_aca_count(aca) == 2);
    hddvd_aca_close(aca);
    hddvd_stream_close(stream);
    return rc;
}

static void test_archive_limits(void)
{
    unsigned char bytes[256];
    size_t size = build_aca(bytes);
    hddvd_config_limits config;
    hddvd_config_limits_default(&config);
    assert(open_aca(bytes, size, &config) == HDDVD_OK);
    config.max_archive_members = 1;
    assert(open_aca(bytes, size, &config) == HDDVD_ELIMIT);
    hddvd_config_limits_default(&config);
    config.max_archive_member_bytes = 4;
    assert(open_aca(bytes, size, &config) == HDDVD_ELIMIT);
}

static void test_xml_limits(void)
{
    size_t xmf_size, xmu_size, xpl_size;
    unsigned char *xmf_bytes = read_fixture("plain_manifest.xmf", &xmf_size);
    unsigned char *xmu_bytes = read_fixture("plain_markup.xmu", &xmu_size);
    unsigned char *xpl_bytes = read_fixture("playlist_full.xpl", &xpl_size);
    hddvd_config_limits config;
    hddvd_xmf *xmf = NULL;
    hddvd_xmu *xmu = NULL;
    hddvd_xpl *xpl = NULL;

    hddvd_config_limits_default(&config);
    assert(hddvd_xmf_open_with_config(xmf_bytes, xmf_size, &config, &xmf) == HDDVD_OK);
    hddvd_xmf_close(xmf);
    assert(hddvd_xmu_open_with_config(xmu_bytes, xmu_size, &config, &xmu) == HDDVD_OK);
    hddvd_xmu_close(xmu);
    assert(hddvd_xpl_open_with_config(xpl_bytes, xpl_size, &config, &xpl) == HDDVD_OK);
    hddvd_xpl_close(xpl);

    config.max_xml_nodes = 1;
    assert(hddvd_xmf_open_with_config(xmf_bytes, xmf_size, &config, &xmf) == HDDVD_ELIMIT);
    assert(hddvd_xmu_open_with_config(xmu_bytes, xmu_size, &config, &xmu) == HDDVD_ELIMIT);
    assert(hddvd_xpl_open_with_config(xpl_bytes, xpl_size, &config, &xpl) == HDDVD_ELIMIT);

    hddvd_config_limits_default(&config);
    config.max_references = 1;
    assert(hddvd_xmf_open_with_config(xmf_bytes, xmf_size, &config, &xmf) == HDDVD_ELIMIT);

    hddvd_config_limits_default(&config);
    config.max_xml_depth = 1;
    assert(hddvd_xmu_open_with_config(xmu_bytes, xmu_size, &config, &xmu) == HDDVD_ELIMIT);

    hddvd_config_limits_default(&config);
    config.max_xml_attributes = 1;
    assert(hddvd_xmu_open_with_config(xmu_bytes, xmu_size, &config, &xmu) == HDDVD_ELIMIT);

    hddvd_config_limits_default(&config);
    config.max_playlist_items = 1;
    assert(hddvd_xpl_open_with_config(xpl_bytes, xpl_size, &config, &xpl) == HDDVD_ELIMIT);

    assert(xmf == NULL && xmu == NULL && xpl == NULL);
    free(xmf_bytes);
    free(xmu_bytes);
    free(xpl_bytes);
}

/* Two strings, two functions and two instructions in the clean-room layout
   described in docs/formats/hdi-script.md. */
static size_t build_script(unsigned char *out)
{
    const size_t strings = 32, functions = strings + 10, instructions = functions + 24;
    memset(out, 0, instructions + 16);
    memcpy(out, "HDIS", 4);
    put16(out + 4, 1);
    put16(out + 6, 32);
    put32(out + 8, 2);
    put32(out + 12, 2);
    put32(out + 16, 2);
    put32(out + 20, (uint32_t)strings);
    put32(out + 24, (uint32_t)functions);
    put32(out + 28, (uint32_t)instructions);
    put32(out + strings, 1);
    out[strings + 4] = 'a';
    put32(out + strings + 5, 1);
    out[strings + 9] = 'b';
    put32(out + functions, 0);
    put32(out + functions + 4, 0);
    put32(out + functions + 8, 1);
    put32(out + functions + 12, 1);
    put32(out + functions + 16, 1);
    put32(out + functions + 20, 1);
    out[instructions] = 0x20;
    out[instructions + 8] = 0x20;
    put32(out + instructions + 12, 1);
    return instructions + 16;
}

static void test_script_limits(void)
{
    unsigned char bytes[128];
    size_t size = build_script(bytes);
    hddvd_config_limits config;
    hddvd_script *script = NULL;
    hddvd_config_limits_default(&config);
    assert(hddvd_script_open_with_config(bytes, size, &config, &script) == HDDVD_OK);
    assert(hddvd_script_string_count(script) == 2 && hddvd_script_function_count(script) == 2);
    hddvd_script_close(script);
    script = NULL;
    config.max_script_strings = 1;
    assert(hddvd_script_open_with_config(bytes, size, &config, &script) == HDDVD_ELIMIT);
    hddvd_config_limits_default(&config);
    config.max_script_functions = 1;
    assert(hddvd_script_open_with_config(bytes, size, &config, &script) == HDDVD_ELIMIT);
    hddvd_config_limits_default(&config);
    config.max_script_instructions = 1;
    assert(hddvd_script_open_with_config(bytes, size, &config, &script) == HDDVD_ELIMIT);
    hddvd_config_limits_default(&config);
    config.max_script_bytes = size - 1;
    assert(hddvd_script_open_with_config(bytes, size, &config, &script) == HDDVD_ELIMIT);
    assert(script == NULL);
}

/* The config's script heap, recursion, instruction and byte limits also
   bound the script runtime, when the build has one. */
static void test_runtime_limits(void)
{
    static const char loop[] = "for (var i = 0; i < 200000; i++) {}";
    static const char deep[] = "function d(n) { return n ? d(n - 1) : 0; } d(100000);";
    static const char grow[] = "var a = []; for (var i = 0; i < 200000; i++) a.push('x' + i);";
    hddvd_config_limits config;
    hddvd_runtime *runtime = NULL;
    hddvd_runtime_diagnostic out;
    if (!hddvd_runtime_engine())
        return;
    hddvd_config_limits_default(&config);
    assert(hddvd_runtime_create_with_config(&config, &runtime) == HDDVD_OK);
    assert(hddvd_runtime_evaluate(runtime, "loop", loop, sizeof(loop) - 1) == HDDVD_OK);
    hddvd_runtime_destroy(runtime);

    config.max_script_instructions = 10000;
    assert(hddvd_runtime_create_with_config(&config, &runtime) == HDDVD_OK);
    assert(hddvd_runtime_evaluate(runtime, "loop", loop, sizeof(loop) - 1) == HDDVD_ELIMIT);
    assert(hddvd_runtime_get_diagnostic(runtime, &out) == HDDVD_OK);
    assert(out.limit == HDDVD_RUNTIME_LIMIT_OPERATIONS);
    hddvd_runtime_destroy(runtime);

    hddvd_config_limits_default(&config);
    config.max_script_recursion = 8;
    assert(hddvd_runtime_create_with_config(&config, &runtime) == HDDVD_OK);
    assert(hddvd_runtime_evaluate(runtime, "deep", deep, sizeof(deep) - 1) == HDDVD_ELIMIT);
    assert(hddvd_runtime_get_diagnostic(runtime, &out) == HDDVD_OK);
    assert(out.limit == HDDVD_RUNTIME_LIMIT_RECURSION);
    hddvd_runtime_destroy(runtime);

    hddvd_config_limits_default(&config);
    config.max_script_heap_bytes = 2u * 1024u * 1024u;
    assert(hddvd_runtime_create_with_config(&config, &runtime) == HDDVD_OK);
    assert(hddvd_runtime_evaluate(runtime, "grow", grow, sizeof(grow) - 1) == HDDVD_ELIMIT);
    assert(hddvd_runtime_get_diagnostic(runtime, &out) == HDDVD_OK);
    assert(out.limit == HDDVD_RUNTIME_LIMIT_HEAP);
    hddvd_runtime_destroy(runtime);

    hddvd_config_limits_default(&config);
    config.max_script_bytes = sizeof(loop) - 2;
    assert(hddvd_runtime_create_with_config(&config, &runtime) == HDDVD_OK);
    assert(hddvd_runtime_evaluate(runtime, "loop", loop, sizeof(loop) - 1) == HDDVD_ELIMIT);
    hddvd_runtime_destroy(runtime);
}

static void release_plane(void *opaque, const uint8_t *pixels)
{
    (void)opaque;
    free((void *)pixels);
}
/* Every image decodes to 20x10 pixels. */
static int decode(void *opaque, const char *id, const void *data, size_t size,
                  hddvd_image_plane *plane)
{
    (void)opaque;
    (void)id;
    (void)data;
    (void)size;
    plane->pixels = calloc(1, 4u * 20u * 10u);
    if (!plane->pixels)
        return HDDVD_ENOMEM;
    plane->width = 20;
    plane->height = 10;
    plane->stride = 80;
    plane->release = release_plane;
    plane->release_opaque = NULL;
    return HDDVD_OK;
}

static int build_scene(const hddvd_config_limits *config, size_t encoded_size)
{
    static const uint8_t encoded[2] = {1, 2};
    hddvd_scene_node nodes[2];
    hddvd_image_cache *cache = NULL;
    hddvd_display_list *list = NULL;
    size_t i;
    int rc;
    memset(nodes, 0, sizeof(nodes));
    for (i = 0; i < 2; i++)
    {
        nodes[i].id = i + 1;
        nodes[i].parent = SIZE_MAX;
        nodes[i].bounds = (hddvd_rect){(int32_t)(i * 100), 0, 20, 10};
        nodes[i].opacity = 255;
        nodes[i].visible = 1;
        nodes[i].normal_image.identity = i ? "second" : "first";
        nodes[i].normal_image.encoded = encoded;
        nodes[i].normal_image.encoded_size = encoded_size;
    }
    assert(hddvd_image_cache_create_with_config(decode, NULL, config, &cache) == HDDVD_OK);
    rc = hddvd_display_build(cache, 1920, 1080, nodes, 2, NULL, &list);
    assert((rc == HDDVD_OK) == (list != NULL));
    if (list)
        assert(hddvd_display_quad_count(list) == 2);
    hddvd_display_list_close(list);
    hddvd_image_cache_destroy(cache);
    return rc;
}

static void test_display_limits(void)
{
    hddvd_config_limits config;
    hddvd_config_limits_default(&config);
    assert(build_scene(&config, 2) == HDDVD_OK);
    config.max_quads = 1;
    assert(build_scene(&config, 2) == HDDVD_ELIMIT);
    hddvd_config_limits_default(&config);
    config.max_images = 1;
    assert(build_scene(&config, 2) == HDDVD_ELIMIT);
    hddvd_config_limits_default(&config);
    config.max_encoded_image_bytes = 1;
    assert(build_scene(&config, 2) == HDDVD_ELIMIT);
    hddvd_config_limits_default(&config);
    config.max_decoded_pixels = 20u * 10u * 2u - 1u;
    assert(build_scene(&config, 2) == HDDVD_ELIMIT);
    /* Oversized planes are reported as malformed decoder output. */
    hddvd_config_limits_default(&config);
    config.max_image_dimension = 19;
    assert(build_scene(&config, 2) == HDDVD_EPARSE);
}

static void test_navigation_limits(void)
{
    hddvd_config_limits config;
    hddvd_nav *nav = NULL;
    hddvd_event event;
    hddvd_config_limits_default(&config);
    config.max_queued_events = 1;
    config.max_timers = 1;
    assert(hddvd_nav_create_with_config(&config, NULL, NULL, NULL, &nav) == HDDVD_OK);
    memset(&event, 0, sizeof(event));
    event.type = HDDVD_EVENT_INPUT;
    assert(hddvd_nav_post(nav, &event) == HDDVD_OK);
    assert(hddvd_nav_post(nav, &event) == HDDVD_ELIMIT);
    assert(hddvd_nav_schedule_timer(nav, 1, (hddvd_time){10}) == HDDVD_OK);
    assert(hddvd_nav_schedule_timer(nav, 2, (hddvd_time){10}) == HDDVD_ELIMIT);
    hddvd_nav_destroy(nav);
}

int main(void)
{
    test_invalid_config();
    test_archive_limits();
    test_xml_limits();
    test_script_limits();
    test_runtime_limits();
    test_display_limits();
    test_navigation_limits();
    return 0;
}
