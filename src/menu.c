/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* A menu session: one HDi application loaded from the disc and run for a
   host, which sends input, time and player events and reads back a display
   list and the player commands scripts made. It joins the script runtime,
   the navigation scheduler, the runtime bridge, the scene builder and the
   display list, so a player's overlay code needs none of them itself. */
#define LOGICAL_WIDTH 1920u
#define LOGICAL_HEIGHT 1080u
#define MAX_COMMANDS 64u

typedef struct menu_image
{
    char *url;
    unsigned char *bytes;
    const void *payload;
    size_t size;
} menu_image;

struct hddvd_menu
{
    hddvd_ctx *ctx;
    hddvd_xmf *xmf;
    hddvd_xmu *xmu;
    char *markup_uri;
    hddvd_runtime *runtime;
    hddvd_runtime_bridge *bridge;
    hddvd_nav *nav;
    hddvd_image_cache *cache;
    int draw_images;
    hddvd_scene *scene;
    hddvd_display_list *list;
    uint64_t built_generation;
    int built;
    menu_image *images;
    size_t image_count;
    hddvd_runtime_command commands[MAX_COMMANDS];
    size_t command_first;
    size_t command_count;
    hddvd_video_transform transform;
    hddvd_time now;
    /* The first failure of the events delivered by the current call. */
    int event_rc;
};

static int fail(const hddvd_ctx *ctx, int rc, const char *uri, const char *message)
{
    hddvd_ctx_set_error(ctx, rc, HDDVD_SUBSYSTEM_RESOURCE, uri, message);
    return rc;
}

/* `reference` as a URI: itself when it has a scheme, else a sibling of
   `base`. */
static char *resolve(const char *base, const char *reference)
{
    const char *slash = strrchr(base, '/');
    size_t prefix = slash ? (size_t)(slash - base) + 1 : 0, length = strlen(reference);
    char *out;
    if (strstr(reference, "://"))
        return hddvd_strdup(reference);
    out = malloc(prefix + length + 1);
    if (!out)
        return NULL;
    memcpy(out, base, prefix);
    memcpy(out + prefix, reference, length + 1);
    return out;
}

/* Reads a whole disc resource; the caller frees *out. */
static int read_resource(hddvd_ctx *ctx, const char *uri, unsigned char **out, size_t *size)
{
    hddvd_stream *stream = NULL;
    unsigned char *bytes;
    uint64_t length;
    size_t got = 0;
    int rc = hddvd_open_resource(ctx, uri, &stream);
    if (rc)
        return rc;
    length = hddvd_stream_size(stream);
    bytes = length < SIZE_MAX ? malloc((size_t)length + 1) : NULL;
    if (!bytes)
        rc = HDDVD_ENOMEM;
    else
        rc = hddvd_stream_read_at(stream, 0, bytes, (size_t)length, &got);
    hddvd_stream_close(stream);
    if (!rc && got != length)
        rc = HDDVD_EIO;
    if (rc)
    {
        free(bytes);
        return rc;
    }
    *out = bytes;
    *size = (size_t)length;
    return HDDVD_OK;
}

static int no_decoder(void *opaque, const char *id, const void *data, size_t size,
                      hddvd_image_plane *plane)
{
    (void)opaque;
    (void)id;
    (void)data;
    (void)size;
    (void)plane;
    return HDDVD_ENOTSUP;
}

/* The scene builder's images: read once from the disc, relative to the
   markup, and kept until close. An image that cannot be read is not drawn. */
static int menu_image_bytes(void *opaque, const char *url, const void **data, size_t *size)
{
    hddvd_menu *menu = (hddvd_menu *)opaque;
    menu_image *image, *more;
    unsigned char *owned = NULL;
    char *uri;
    size_t i;
    int rc;
    if (!menu->draw_images)
        return HDDVD_ENOTFOUND;
    for (i = 0; i < menu->image_count; i++)
        if (!strcmp(menu->images[i].url, url))
        {
            if (!menu->images[i].payload)
                return HDDVD_ENOTFOUND;
            *data = menu->images[i].payload;
            *size = menu->images[i].size;
            return HDDVD_OK;
        }
    more = realloc(menu->images, (menu->image_count + 1) * sizeof(*more));
    if (!more)
        return HDDVD_ENOMEM;
    menu->images = more;
    image = &more[menu->image_count];
    memset(image, 0, sizeof(*image));
    image->url = hddvd_strdup(url);
    uri = resolve(menu->markup_uri, url);
    if (!image->url || !uri)
    {
        free(image->url);
        free(uri);
        return HDDVD_ENOMEM;
    }
    menu->image_count++;
    rc = read_resource(menu->ctx, uri, &image->bytes, &image->size);
    free(uri);
    if (!rc)
    {
        rc = hddvd_object_unwrap(image->bytes, image->size, HDDVD_OBJECT_IMAGE, &owned,
                                 &image->payload, &image->size);
        if (owned)
        {
            free(image->bytes);
            image->bytes = owned;
        }
    }
    if (rc)
    {
        image->payload = NULL;
        return HDDVD_ENOTFOUND;
    }
    *data = image->payload;
    *size = image->size;
    return HDDVD_OK;
}

static void on_event(void *opaque, const hddvd_event *event)
{
    hddvd_menu *menu = (hddvd_menu *)opaque;
    int rc = hddvd_runtime_bridge_event(menu->bridge, event);
    if (rc && !menu->event_rc)
        menu->event_rc = rc;
}

/* The bridge hands over at most 64 commands that are not acknowledged, and
   a command leaves this queue before it can be acknowledged. */
static void on_command(void *opaque, const hddvd_runtime_command *command)
{
    hddvd_menu *menu = (hddvd_menu *)opaque;
    if (menu->command_count < MAX_COMMANDS)
        menu->commands[(menu->command_first + menu->command_count++) % MAX_COMMANDS] = *command;
}

/* Delivers the events that are due and returns the first failure. */
static int deliver(hddvd_menu *menu)
{
    int rc;
    menu->event_rc = HDDVD_OK;
    rc = hddvd_nav_advance(menu->nav, menu->now);
    hddvd_nav_pump(menu->nav, 0);
    return rc ? rc : menu->event_rc;
}

static int post(hddvd_menu *menu, hddvd_event_type type, hddvd_event *event)
{
    event->type = type;
    event->when = menu->now;
    return hddvd_nav_post(menu->nav, event);
}

/* Evaluates one of the manifest's scripts, from its envelope if it has one. */
static int run_script(hddvd_menu *menu, const char *manifest_uri, const char *src)
{
    unsigned char *bytes = NULL, *owned = NULL;
    const void *source;
    size_t size = 0;
    char *uri = resolve(manifest_uri, src);
    int rc;
    if (!uri)
        return HDDVD_ENOMEM;
    rc = read_resource(menu->ctx, uri, &bytes, &size);
    if (!rc)
        rc = hddvd_object_unwrap(bytes, size, HDDVD_OBJECT_SCRIPT, &owned, &source, &size);
    if (rc)
        fail(menu->ctx, rc, uri, "script cannot be read");
    else
        rc = hddvd_runtime_evaluate(menu->runtime, uri, source, size);
    free(owned);
    free(bytes);
    free(uri);
    return rc;
}

/* Tells the runtime which title runs this application, and the title's
   applications in z-order. */
static int place_in_playlist(hddvd_menu *menu, const hddvd_xpl *xpl, const char *manifest_uri)
{
    size_t t, a, k;
    for (t = 0; t < hddvd_xpl_title_count(xpl); t++)
    {
        const char *ids[63];
        size_t n = hddvd_xpl_application_count(xpl, t), order[63], count = 0, self = SIZE_MAX;
        int32_t z[63];
        for (a = 0; a < n && count < 63; a++)
        {
            hddvd_xpl_application app;
            int32_t value;
            if (hddvd_xpl_get_application(xpl, t, a, &app) || !app.src)
                continue;
            value = app.has_z_order ? app.z_order : 0;
            /* Insertion by zOrder keeps playlist order among equals. */
            for (k = count; k > 0 && z[k - 1] > value; k--)
            {
                z[k] = z[k - 1];
                order[k] = order[k - 1];
            }
            z[k] = value;
            order[k] = a;
            count++;
        }
        for (k = 0; k < count; k++)
        {
            hddvd_xpl_application app;
            hddvd_xpl_get_application(xpl, t, order[k], &app);
            ids[k] = "";
            if (hddvd_ascii_equal_ci(app.src, manifest_uri))
            {
                const char *id = hddvd_xmf_application_id(menu->xmf);
                self = k;
                ids[k] = id ? id : "";
            }
        }
        if (self != SIZE_MAX)
        {
            hddvd_runtime_player player;
            int rc = hddvd_runtime_set_applications(menu->runtime, ids, count, self);
            memset(&player, 0, sizeof(player));
            player.title = t;
            player.state = HDDVD_PLAYBACK_PLAYING;
            return rc ? rc : hddvd_runtime_set_player(menu->runtime, &player);
        }
    }
    return HDDVD_OK;
}

void hddvd_menu_close(hddvd_menu *menu)
{
    size_t i;
    if (!menu)
        return;
    hddvd_display_list_close(menu->list);
    hddvd_scene_close(menu->scene);
    hddvd_image_cache_destroy(menu->cache);
    hddvd_runtime_bridge_destroy(menu->bridge);
    hddvd_nav_destroy(menu->nav);
    hddvd_runtime_destroy(menu->runtime);
    hddvd_xmu_close(menu->xmu);
    hddvd_xmf_close(menu->xmf);
    for (i = 0; i < menu->image_count; i++)
    {
        free(menu->images[i].url);
        free(menu->images[i].bytes);
    }
    free(menu->images);
    free(menu->markup_uri);
    free(menu);
}

int hddvd_menu_open(hddvd_ctx *ctx, const hddvd_xpl *playlist, const char *manifest_uri,
                    hddvd_image_decode_fn decode, void *decode_opaque, hddvd_menu **out)
{
    hddvd_menu *menu;
    unsigned char *bytes = NULL;
    const char *id, *src;
    size_t size = 0, i;
    int rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!ctx || !manifest_uri)
        return HDDVD_EINVAL;
    menu = calloc(1, sizeof(*menu));
    if (!menu)
        return HDDVD_ENOMEM;
    menu->ctx = ctx;
    menu->draw_images = decode != NULL;
    hddvd_video_transform_fit(LOGICAL_WIDTH, LOGICAL_HEIGHT, LOGICAL_WIDTH, LOGICAL_HEIGHT,
                              &menu->transform);

    rc = hddvd_runtime_create_with_config(&ctx->limits, &menu->runtime);
    if (!rc)
        rc = hddvd_runtime_bridge_create(menu->runtime, on_command, menu, &menu->bridge);
    if (!rc)
        rc = hddvd_nav_create_with_config(&ctx->limits, on_event, NULL, menu, &menu->nav);
    if (!rc)
        rc = hddvd_image_cache_create_with_config(decode ? decode : no_decoder, decode_opaque,
                                                  &ctx->limits, &menu->cache);
    if (!rc)
        rc = read_resource(ctx, manifest_uri, &bytes, &size);
    if (!rc)
    {
        rc = hddvd_xmf_open_with_config(bytes, size, &ctx->limits, &menu->xmf);
        if (rc)
            fail(ctx, rc, manifest_uri, "invalid manifest");
    }
    free(bytes);
    bytes = NULL;
    if (!rc && playlist)
    {
        rc = hddvd_runtime_load_playlist(menu->runtime, playlist);
        if (!rc)
            rc = place_in_playlist(menu, playlist, manifest_uri);
    }
    /* A script that fails does not stop the others or the markup: the
       failure is in the runtime's diagnostic and issues, as on a player
       that goes on with what loaded. */
    for (i = 0; !rc && i < hddvd_xmf_script_count(menu->xmf); i++)
    {
        int script_rc;
        hddvd_xmf_get_script(menu->xmf, i, &id, &src);
        script_rc = run_script(menu, manifest_uri, src);
        if (script_rc == HDDVD_ENOMEM)
            rc = script_rc;
    }
    if (!rc && hddvd_xmf_markup_count(menu->xmf))
    {
        hddvd_xmf_get_markup(menu->xmf, 0, &id, &src);
        menu->markup_uri = resolve(manifest_uri, src);
        rc = menu->markup_uri ? read_resource(ctx, menu->markup_uri, &bytes, &size) : HDDVD_ENOMEM;
        if (!rc)
        {
            rc = hddvd_xmu_open_with_config(bytes, size, &ctx->limits, &menu->xmu);
            if (!rc)
                rc = hddvd_runtime_load_markup(menu->runtime, menu->xmu);
            if (rc)
                fail(ctx, rc, menu->markup_uri, "invalid markup");
        }
        free(bytes);
    }
    if (rc)
    {
        hddvd_menu_close(menu);
        return rc;
    }
    /* The handler's own failures are the runtime's to report. */
    (void)hddvd_runtime_markup_loaded(menu->runtime);
    hddvd_clear_error(ctx);
    *out = menu;
    return HDDVD_OK;
}

hddvd_runtime *hddvd_menu_runtime(const hddvd_menu *menu)
{
    return menu ? menu->runtime : NULL;
}

int hddvd_menu_set_viewport(hddvd_menu *menu, uint32_t width, uint32_t height)
{
    if (!menu || !width || !height)
        return HDDVD_EINVAL;
    hddvd_video_transform_fit(LOGICAL_WIDTH, LOGICAL_HEIGHT, width, height, &menu->transform);
    return HDDVD_OK;
}

int hddvd_menu_get_transform(const hddvd_menu *menu, hddvd_video_transform *out)
{
    if (!menu || !out)
        return HDDVD_EINVAL;
    *out = menu->transform;
    return HDDVD_OK;
}

int hddvd_menu_advance(hddvd_menu *menu, hddvd_time now)
{
    hddvd_event event;
    int rc;
    if (!menu || now.ticks < menu->now.ticks)
        return HDDVD_EINVAL;
    menu->now = now;
    memset(&event, 0, sizeof(event));
    rc = post(menu, HDDVD_EVENT_CLOCK, &event);
    return rc ? rc : deliver(menu);
}

int hddvd_menu_seek(hddvd_menu *menu, hddvd_time now)
{
    int rc;
    if (!menu)
        return HDDVD_EINVAL;
    menu->now = now;
    menu->event_rc = HDDVD_OK;
    rc = hddvd_nav_discontinuity(menu->nav, now);
    hddvd_nav_pump(menu->nav, 0);
    return rc ? rc : menu->event_rc;
}

int hddvd_menu_post(hddvd_menu *menu, const hddvd_event *event)
{
    hddvd_event copy;
    int rc;
    if (!menu || !event)
        return HDDVD_EINVAL;
    copy = *event;
    rc = post(menu, event->type, &copy);
    return rc ? rc : deliver(menu);
}

int hddvd_menu_key(hddvd_menu *menu, uint32_t key, int pressed)
{
    hddvd_event event;
    int rc;
    if (!menu)
        return HDDVD_EINVAL;
    memset(&event, 0, sizeof(event));
    event.data.input.key = key;
    event.data.input.pressed = pressed ? 1 : 0;
    rc = post(menu, HDDVD_EVENT_INPUT, &event);
    return rc ? rc : deliver(menu);
}

/* Rebuilds the scene and display list when the runtime changed something. */
static int rebuild(hddvd_menu *menu, int *changed)
{
    hddvd_scene *scene = NULL;
    hddvd_display_list *list = NULL;
    uint64_t generation = hddvd_runtime_generation(menu->runtime);
    int rc;
    if (changed)
        *changed = 0;
    if (!menu->xmu || (menu->built && generation == menu->built_generation))
        return HDDVD_OK;
    rc = hddvd_scene_build(menu->xmu, hddvd_runtime_styles(menu->runtime),
                           hddvd_runtime_focus(menu->runtime), menu_image_bytes, menu, &scene);
    if (!rc)
        rc = hddvd_display_build_scene(menu->cache, LOGICAL_WIDTH, LOGICAL_HEIGHT, scene,
                                       menu->list, &list);
    if (rc)
    {
        hddvd_scene_close(scene);
        return rc;
    }
    /* The old list goes before the scene it borrows from. */
    hddvd_display_list_close(menu->list);
    hddvd_scene_close(menu->scene);
    menu->list = list;
    menu->scene = scene;
    menu->built = 1;
    menu->built_generation = generation;
    if (changed)
        *changed = 1;
    return HDDVD_OK;
}

int hddvd_menu_update(hddvd_menu *menu, const hddvd_display_list **list, int *changed)
{
    int rc;
    if (list)
        *list = NULL;
    if (changed)
        *changed = 0;
    if (!menu || !list)
        return HDDVD_EINVAL;
    rc = rebuild(menu, changed);
    *list = menu->list;
    return rc;
}

int hddvd_menu_pointer(hddvd_menu *menu, int32_t x, int32_t y, int pressed)
{
    hddvd_event event;
    uint64_t id = 0;
    size_t node = SIZE_MAX;
    int32_t lx, ly;
    int rc;
    if (!menu)
        return HDDVD_EINVAL;
    /* What is under the pointer is what the host last could have drawn. */
    rc = rebuild(menu, NULL);
    if (rc)
        return rc;
    if (menu->list && hddvd_video_to_logical(&menu->transform, x, y, &lx, &ly) == HDDVD_OK &&
        hddvd_display_hit_test(menu->list, lx, ly, &id) == HDDVD_OK)
        node = (size_t)id;
    if (!menu->xmu)
        return HDDVD_OK;
    rc = hddvd_runtime_pointer(menu->runtime, node, pressed);
    /* A clock event at the current time, so the bridge hands over the
       commands scripts made in answer. */
    memset(&event, 0, sizeof(event));
    if (post(menu, HDDVD_EVENT_CLOCK, &event) == HDDVD_OK)
    {
        int delivered = deliver(menu);
        if (!rc)
            rc = delivered;
    }
    return rc;
}

int hddvd_menu_take_command(hddvd_menu *menu, hddvd_runtime_command *out)
{
    if (!menu || !out)
        return HDDVD_EINVAL;
    if (!menu->command_count)
        return HDDVD_ENOTFOUND;
    *out = menu->commands[menu->command_first];
    menu->command_first = (menu->command_first + 1) % MAX_COMMANDS;
    menu->command_count--;
    return HDDVD_OK;
}

int hddvd_menu_acknowledge(hddvd_menu *menu, uint64_t id, hddvd_command_result result)
{
    hddvd_event event;
    int rc;
    if (!menu)
        return HDDVD_EINVAL;
    memset(&event, 0, sizeof(event));
    event.data.acknowledgement.command_id = id;
    event.data.acknowledgement.result = result;
    rc = post(menu, HDDVD_EVENT_SCRIPT_ACK, &event);
    return rc ? rc : deliver(menu);
}
