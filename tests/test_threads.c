#include "hddvd.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each thread uses its own objects of every kind at the same time as the
   others. libhddvd keeps no mutable global state, so the results must match
   a single-threaded run, and ThreadSanitizer (the CI tsan job) must report
   no races. */
#define THREADS 4
#define ITERATIONS 25

struct input
{
    unsigned char *playlist, *markup, *map, *vti;
    size_t playlist_size, markup_size, map_size, vti_size;
};

struct outcome
{
    size_t titles;
    size_t nodes;
    size_t unmodeled;
    uint64_t sectors;
    uint16_t evob;
    size_t events;
    size_t quads;
    int script;
};

static const struct input *shared_input;

static unsigned char *read_fixture(const char *name, size_t *size)
{
    char path[512];
    FILE *f;
    long end;
    unsigned char *bytes;
    snprintf(path, sizeof(path), "%s/%s", HDDVD_FIXTURE_DIR, name);
    f = fopen(path, "rb");
    assert(f && !fseek(f, 0, SEEK_END) && (end = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET));
    bytes = malloc((size_t)end);
    assert(bytes && fread(bytes, 1, (size_t)end, f) == (size_t)end);
    fclose(f);
    *size = (size_t)end;
    return bytes;
}

static void count_event(void *opaque, const hddvd_event *event)
{
    (void)event;
    ++*(size_t *)opaque;
}

static void release(void *opaque, const uint8_t *pixels)
{
    (void)opaque;
    free((void *)pixels);
}

static int decode(void *opaque, const char *id, const void *data, size_t size,
                  hddvd_image_plane *plane)
{
    uint8_t *pixels = calloc(1, 4u * 4u * 4u);
    (void)opaque;
    (void)id;
    (void)data;
    (void)size;
    if (!pixels)
        return HDDVD_ENOMEM;
    memset(plane, 0, sizeof(*plane));
    plane->pixels = pixels;
    plane->width = 4;
    plane->height = 4;
    plane->stride = 16;
    plane->release = release;
    return HDDVD_OK;
}

static void run_once(struct outcome *out)
{
    const struct input *in = shared_input;
    hddvd_xpl *xpl = NULL;
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    hddvd_stream *stream = NULL;
    hddvd_tmap *tmap = NULL;
    hddvd_vti *vti = NULL;
    hddvd_nav *nav = NULL;
    hddvd_image_cache *cache = NULL;
    hddvd_display_list *list = NULL;
    hddvd_event event;
    hddvd_time t;
    size_t i;
    static const uint8_t encoded[] = {1};
    hddvd_scene_node nodes[2];

    assert(hddvd_xpl_open(in->playlist, in->playlist_size, &xpl) == HDDVD_OK);
    out->titles = hddvd_xpl_title_count(xpl);
    hddvd_xpl_close(xpl);

    assert(hddvd_xmu_open(in->markup, in->markup_size, &xmu) == HDDVD_OK);
    assert(hddvd_xmu_styles_open(xmu, &styles) == HDDVD_OK);
    out->nodes = hddvd_xmu_node_count(xmu);
    out->unmodeled = hddvd_xmu_styles_unmodeled_count(styles);
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);

    assert(hddvd_stream_open_memory(in->map, in->map_size, &stream) == HDDVD_OK);
    assert(hddvd_tmap_open(stream, &tmap) == HDDVD_OK);
    hddvd_stream_close(stream);
    assert(hddvd_stream_open_memory(in->vti, in->vti_size, &stream) == HDDVD_OK);
    assert(hddvd_vti_open(stream, &vti) == HDDVD_OK);
    hddvd_stream_close(stream);
    out->sectors = hddvd_tmap_sector_count(tmap);
    out->evob = hddvd_tmap_evob_number(tmap);
    hddvd_tmap_close(tmap);
    hddvd_vti_close(vti);

    out->events = 0;
    assert(hddvd_nav_create(NULL, count_event, NULL, &out->events, &nav) == HDDVD_OK);
    memset(&event, 0, sizeof(event));
    event.type = HDDVD_EVENT_CLOCK;
    for (i = 0; i < 8; i++)
    {
        event.when.ticks = i * 1000u;
        assert(hddvd_nav_post(nav, &event) == HDDVD_OK);
    }
    assert(hddvd_nav_schedule_timer(nav, 1, (hddvd_time){4500}) == HDDVD_OK);
    t.ticks = 10000;
    assert(hddvd_nav_advance(nav, t) == HDDVD_OK);
    (void)hddvd_nav_pump(nav, SIZE_MAX);
    hddvd_nav_destroy(nav);

    memset(nodes, 0, sizeof(nodes));
    nodes[0].id = 1;
    nodes[0].parent = SIZE_MAX;
    nodes[0].bounds = (hddvd_rect){0, 0, 100, 100};
    nodes[0].opacity = 255;
    nodes[0].visible = 1;
    nodes[1] = nodes[0];
    nodes[1].id = 2;
    nodes[1].parent = 0;
    nodes[1].bounds = (hddvd_rect){10, 10, 4, 4};
    nodes[1].normal_image = (hddvd_image_source){"image", encoded, 1};
    assert(hddvd_image_cache_create(decode, NULL, NULL, &cache) == HDDVD_OK);
    assert(hddvd_display_build(cache, 1920, 1080, nodes, 2, NULL, &list) == HDDVD_OK);
    out->quads = hddvd_display_quad_count(list);
    hddvd_display_list_close(list);
    hddvd_image_cache_destroy(cache);

    /* Separate script runtimes, when the build has an engine. */
    if (hddvd_runtime_engine())
    {
        static const char source[] = "var n = 0; function work() {"
                                     "  for (var i = 0; i < 1000; i++) n += Math.random() < 2;"
                                     "  if (n % 1000) throw new Error('n ' + n); }";
        hddvd_runtime *runtime = NULL;
        assert(hddvd_runtime_create(&runtime) == HDDVD_OK);
        assert(hddvd_runtime_evaluate(runtime, "work", source, sizeof(source) - 1) == HDDVD_OK);
        out->script = hddvd_runtime_call(runtime, "work") == HDDVD_OK &&
                      hddvd_runtime_call(runtime, "work") == HDDVD_OK;
        hddvd_runtime_destroy(runtime);
    }
}

static void *worker(void *opaque)
{
    struct outcome *result = opaque;
    struct outcome current;
    int i;
    for (i = 0; i < ITERATIONS; i++)
    {
        /* Zeroed so padding bytes compare equal. */
        memset(&current, 0, sizeof(current));
        run_once(&current);
        assert(!memcmp(&current, result, sizeof(current)));
    }
    return NULL;
}

int main(void)
{
    struct input in;
    struct outcome expected, results[THREADS];
    pthread_t threads[THREADS];
    int i;
    in.playlist = read_fixture("playlist_full.xpl", &in.playlist_size);
    in.markup = read_fixture("fuzz/xmu/style-details.xmu", &in.markup_size);
    in.map = read_fixture("timemap/original.map", &in.map_size);
    in.vti = read_fixture("timemap/original.vti", &in.vti_size);
    shared_input = &in;

    memset(&expected, 0, sizeof(expected));
    run_once(&expected);
    assert(expected.titles == 3 && expected.unmodeled == 2 && expected.sectors == 131 &&
           expected.evob == 1 && expected.events > 0 && expected.quads > 0 &&
           expected.script == (hddvd_runtime_engine() != NULL));
    for (i = 0; i < THREADS; i++)
    {
        results[i] = expected;
        assert(pthread_create(&threads[i], NULL, worker, &results[i]) == 0);
    }
    for (i = 0; i < THREADS; i++)
        assert(pthread_join(threads[i], NULL) == 0);
    free(in.playlist);
    free(in.markup);
    free(in.map);
    free(in.vti);
    return 0;
}
