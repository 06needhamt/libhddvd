/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Members read for the scene view, kept alive until the display list is
   closed. */
struct members
{
    const hddvd_aca *aca;
    void **blobs;
    size_t count;
};

static int read_member(const hddvd_aca *aca, const char *name, void **out, size_t *size)
{
    hddvd_stream *stream = NULL;
    uint64_t length;
    size_t got = 0;
    void *bytes;
    int rc = hddvd_aca_open_member(aca, name, &stream);
    if (rc)
        return rc;
    length = hddvd_stream_size(stream);
    bytes = malloc(length ? (size_t)length : 1);
    if (!bytes || length > SIZE_MAX)
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

/* Images are looked up by base name among the archive's members. */
static int scene_image(void *opaque, const char *url, const void **data, size_t *size)
{
    struct members *m = opaque;
    const char *base = strrchr(url, '/');
    void **grown;
    void *bytes;
    int rc = read_member(m->aca, base ? base + 1 : url, &bytes, size);
    if (rc)
        return rc;
    grown = realloc(m->blobs, (m->count + 1) * sizeof(*grown));
    if (!grown)
    {
        free(bytes);
        return HDDVD_ENOMEM;
    }
    m->blobs = grown;
    m->blobs[m->count++] = bytes;
    *data = bytes;
    return HDDVD_OK;
}

static void release_plane(void *opaque, const uint8_t *pixels)
{
    (void)opaque;
    free((void *)pixels);
}

/* Layout does not depend on pixels, so every image is a 1x1 stand-in. */
static int stand_in(void *opaque, const char *id, const void *data, size_t size,
                    hddvd_image_plane *plane)
{
    (void)opaque;
    (void)id;
    (void)data;
    (void)size;
    memset(plane, 0, sizeof(*plane));
    plane->pixels = calloc(1, 4);
    plane->width = 1;
    plane->height = 1;
    plane->stride = 4;
    plane->release = release_plane;
    return plane->pixels ? HDDVD_OK : HDDVD_ENOMEM;
}

static const char *markup_label(const hddvd_xmu *xmu, uint64_t index, char *buffer, size_t size)
{
    hddvd_xmu_node node;
    if (hddvd_xmu_get_node(xmu, (size_t)index, &node))
        return "?";
    snprintf(buffer, size, "%s#%s", node.name, node.id ? node.id : "-");
    return buffer;
}

/* Lists every scene node, then the quads the display list draws. */
static int report_scene(const char *archive, const char *markup, const char *focus_id)
{
    struct members members = {NULL, NULL, 0};
    hddvd_aca *aca = NULL;
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    hddvd_scene *scene = NULL;
    hddvd_image_cache *cache = NULL;
    hddvd_display_list *list = NULL;
    hddvd_display_limits limits;
    const hddvd_scene_node *nodes;
    size_t i, size = 0, focused = SIZE_MAX, visible = 0;
    void *bytes = NULL;
    char label[160];
    int rc = hddvd_aca_open(archive, &aca);
    members.aca = aca;
    if (!rc)
        rc = read_member(aca, markup, &bytes, &size);
    if (!rc)
        rc = hddvd_xmu_open(bytes, size, &xmu);
    free(bytes);
    if (!rc)
        rc = hddvd_xmu_styles_open(xmu, &styles);
    if (!rc && focus_id && (focused = hddvd_xmu_find_id(xmu, focus_id)) == SIZE_MAX)
        rc = HDDVD_ENOTFOUND;
    if (!rc)
        rc = hddvd_scene_build(xmu, styles, focused, scene_image, &members, &scene);
    hddvd_display_limits_default(&limits);
    limits.max_images = SIZE_MAX;
    limits.max_quads = SIZE_MAX;
    if (!rc)
        rc = hddvd_image_cache_create(stand_in, NULL, &limits, &cache);
    if (!rc)
        rc = hddvd_display_build_scene(cache, 1920, 1080, scene, NULL, &list);
    if (rc)
        fprintf(stderr, "Scene inspection failed: %s\n", hddvd_strerror(rc));
    nodes = hddvd_scene_nodes(scene);
    for (i = 0; !rc && i < hddvd_scene_node_count(scene); i++)
    {
        const hddvd_scene_node *n = &nodes[i];
        char parent[160];
        visible += n->visible;
        printf("Node %zu %s parent=%s bounds=%d,%d %ux%u z=%d opacity=%u visible=%u%s%s "
               "image=%s focus-image=%s\n",
               i, markup_label(xmu, n->id, label, sizeof(label)),
               n->parent == SIZE_MAX
                   ? "-"
                   : markup_label(xmu, nodes[n->parent].id, parent, sizeof(parent)),
               n->bounds.x, n->bounds.y, n->bounds.width, n->bounds.height, n->z_index,
               (unsigned)n->opacity, (unsigned)n->visible, n->focused ? " focused" : "",
               n->hit_test ? " hit-test" : "",
               n->normal_image.identity ? n->normal_image.identity : "-",
               n->focused_image.identity ? n->focused_image.identity : "-");
    }
    for (i = 0; !rc && i < hddvd_display_item_count(list); i++)
    {
        static const char *const kinds[] = {"?", "image", "fill", "text"};
        hddvd_display_item item;
        hddvd_display_get_item(list, i, &item);
        printf("Item %zu %s %s destination=%d,%d %ux%u clip=%d,%d %ux%u z=%d opacity=%u", i,
               kinds[item.kind], markup_label(xmu, item.quad.node_id, label, sizeof(label)),
               item.quad.destination.x, item.quad.destination.y, item.quad.destination.width,
               item.quad.destination.height, item.quad.clip.x, item.quad.clip.y,
               item.quad.clip.width, item.quad.clip.height, item.quad.z_index,
               (unsigned)item.quad.opacity);
        if (item.has_color)
            printf(" color=#%08x", (unsigned)item.color);
        if (item.text)
            printf(" font=%s size=%u text=\"%s\"", item.font ? item.font : "-",
                   (unsigned)item.font_size, item.text);
        printf("\n");
    }
    if (!rc)
        printf("Scene nodes=%zu visible=%zu items=%zu image-quads=%zu images=%zu\n",
               hddvd_scene_node_count(scene), visible, hddvd_display_item_count(list),
               hddvd_display_quad_count(list), members.count);
    hddvd_display_list_close(list);
    hddvd_image_cache_destroy(cache);
    hddvd_scene_close(scene);
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
    hddvd_aca_close(aca);
    for (i = 0; i < members.count; i++)
        free(members.blobs[i]);
    free(members.blobs);
    return rc;
}
static int dump_xmf(void *o, const char *n, const void *b, size_t z)
{
    return hddvd_xmf_dump(b, z, n, o);
}
static int report_application(const char *root, const char *manifest)
{
    hddvd_ctx *context = hddvd_open(root);
    hddvd_application *application = NULL;
    size_t i;
    int rc;
    if (!context)
        return HDDVD_EIO;
    rc = hddvd_application_open(context, manifest, &application);
    if (rc)
        fprintf(stderr, "Application inspection failed: %s\n", hddvd_strerror(rc));
    for (i = 0; !rc && i < hddvd_application_dependency_count(application); i++)
    {
        hddvd_dependency dependency;
        hddvd_application_get_dependency(application, i, &dependency);
        printf("Dependency kind=%d id=%s uri=%s status=%s\n", (int)dependency.kind,
               dependency.id ? dependency.id : "-", dependency.uri,
               hddvd_strerror(dependency.status));
    }
    for (i = 0; !rc && i < hddvd_application_requirement_count(application); i++)
    {
        hddvd_requirement requirement;
        hddvd_application_get_requirement(application, i, &requirement);
        printf("Requirement kind=%d namespace=%s name=%s occurrences=%zu supported=%s\n",
               (int)requirement.kind, requirement.namespace_uri ? requirement.namespace_uri : "-",
               requirement.name, requirement.occurrences, requirement.supported ? "yes" : "no");
    }
    hddvd_application_close(application);
    hddvd_close(context);
    return rc;
}
int main(int argc, char **argv)
{
    void *b = NULL;
    size_t n = 0;
    int rc;
    if ((argc == 4 || argc == 5) && !strcmp(argv[1], "--scene"))
        return report_scene(argv[2], argv[3], argc == 5 ? argv[4] : NULL) ? 1 : 0;
    if (argc == 3)
        return report_application(argv[1], argv[2]) ? 1 : 0;
    if (argc != 2)
    {
        fprintf(stderr,
                "Usage: %s archive.aca\n       %s DISC_ROOT MANIFEST_URI\n"
                "       %s --scene archive.aca MARKUP.xmu [FOCUSED_ID]\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }
    rc = hddvd_aca_visit_xmf(argv[1], dump_xmf, stdout);
    if (rc)
    {
        fprintf(stderr, "XMF inspection failed: %s\n", hddvd_strerror(rc));
        return 1;
    }
    rc = hddvd_aca_read_member(argv[1], "main.xmu", &b, &n);
    if (rc == HDDVD_OK)
    {
        rc = hddvd_xmu_dump(b, n, "main.xmu", stdout);
        hddvd_free(b);
    }
    return rc == HDDVD_ENOTFOUND ? 0 : (rc ? 1 : 0);
}
