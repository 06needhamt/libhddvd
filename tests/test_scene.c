#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS "http://www.dvdforum.org/2005/ihd"
#define STYLE "http://www.dvdforum.org/2005/ihd#style"

struct lookup
{
    int calls;
    int fail;
};

static const unsigned char encoded[] = {1, 2, 3};

static int image(void *opaque, const char *url, const void **data, size_t *size)
{
    struct lookup *l = opaque;
    l->calls++;
    if (l->fail)
        return HDDVD_EIO;
    if (!strcmp(url, "missing.png"))
        return HDDVD_ENOTFOUND;
    *data = encoded;
    *size = sizeof(encoded);
    return HDDVD_OK;
}

static void release(void *opaque, const uint8_t *pixels)
{
    (void)opaque;
    free((void *)pixels);
}

static int decode(void *opaque, const char *id, const void *data, size_t size,
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
    plane->release = release;
    return plane->pixels ? HDDVD_OK : HDDVD_ENOMEM;
}

static void open_markup(const char *xml, hddvd_xmu **xmu, hddvd_xmu_styles **styles)
{
    assert(hddvd_xmu_open(xml, strlen(xml), xmu) == HDDVD_OK);
    assert(hddvd_xmu_styles_open(*xmu, styles) == HDDVD_OK);
}

static const hddvd_scene_node *find(const hddvd_scene *scene, size_t markup)
{
    size_t i;
    for (i = 0; i < hddvd_scene_node_count(scene); i++)
        if (hddvd_scene_nodes(scene)[i].id == markup)
            return &hddvd_scene_nodes(scene)[i];
    return NULL;
}

static void builds_scene(void)
{
    static const char xml[] =
        "<root xmlns='" NS "' xmlns:s='" STYLE "'><head><styling>"
        "<style id='half' s:opacity='0.5'/></styling></head><body>"
        "<div id='panel' style='half' s:x='100px' s:y='50px' s:width='1000px' s:height='500px'>"
        "<button id='button' s:width='50%' s:opacity='inherit' s:zIndex='3' "
        "s:backgroundImage=\"url('normal.png') url('focus.png')\">"
        "<div id='fill' s:x='5px'/></button>"
        "<object id='gone' s:display='none' s:backgroundImage=\"url('normal.png')\"/>"
        "<div id='hidden' s:visibility='hidden' s:width='10px' s:height='10px'/>"
        "<div id='lost' s:width='10px' s:height='10px' "
        "s:backgroundImage=\"url('missing.png')\"/>"
        "<param id='param'/></div></body></root>";
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    hddvd_scene *scene = NULL;
    hddvd_image_cache *cache = NULL;
    hddvd_display_list *list = NULL;
    struct lookup lookup = {0, 0};
    const hddvd_scene_node *panel, *button, *fill, *gone, *hidden, *lost;
    size_t button_index;
    uint64_t hit;

    open_markup(xml, &xmu, &styles);
    button_index = hddvd_xmu_find_id(xmu, "button");
    assert(hddvd_scene_build(xmu, styles, button_index, image, &lookup, &scene) == HDDVD_OK);
    /* body's descendants except param, in document order. */
    assert(hddvd_scene_node_count(scene) == 6);
    assert(!find(scene, hddvd_xmu_find_id(xmu, "param")));
    assert(!find(scene, hddvd_xmu_find_id(xmu, "half")));
    panel = find(scene, hddvd_xmu_find_id(xmu, "panel"));
    button = find(scene, button_index);
    fill = find(scene, hddvd_xmu_find_id(xmu, "fill"));
    gone = find(scene, hddvd_xmu_find_id(xmu, "gone"));
    hidden = find(scene, hddvd_xmu_find_id(xmu, "hidden"));
    lost = find(scene, hddvd_xmu_find_id(xmu, "lost"));
    assert(panel && button && fill && gone && hidden && lost);
    assert(panel == hddvd_scene_nodes(scene) && panel->parent == SIZE_MAX);

    /* Geometry: pixels, percentages of the parent, and undeclared sizes
       filling the parent. */
    assert(panel->bounds.x == 100 && panel->bounds.y == 50 && panel->bounds.width == 1000 &&
           panel->bounds.height == 500);
    assert(button->parent == 0 && button->bounds.width == 500 && button->bounds.height == 500);
    assert(fill->parent == 1 && fill->bounds.x == 5 && fill->bounds.width == 500 &&
           fill->bounds.height == 500);

    /* Opacity: 0.5, then inherit (0.5 of 0.5), then the default under it. */
    assert(panel->opacity == 128 && button->opacity == 64 && fill->opacity == 64);

    assert(panel->visible && button->visible && !gone->visible && !hidden->visible);
    assert(button->focused && !panel->focused && button->hit_test && !panel->hit_test);
    assert(button->z_index == 3);
    assert(!strcmp(button->normal_image.identity, "normal.png") &&
           !strcmp(button->focused_image.identity, "focus.png") &&
           button->normal_image.encoded == encoded && button->normal_image.encoded_size == 3);
    /* An image the host cannot supply is simply not drawn. */
    assert(!lost->normal_image.identity && !lost->normal_image.encoded);
    /* normal.png, focus.png and missing.png, each requested once. */
    assert(lookup.calls == 3);

    /* The nodes feed the display list directly; hits name markup nodes. */
    assert(hddvd_image_cache_create(decode, NULL, NULL, &cache) == HDDVD_OK);
    assert(hddvd_display_build(cache, 1920, 1080, hddvd_scene_nodes(scene),
                               hddvd_scene_node_count(scene), NULL, &list) == HDDVD_OK);
    assert(hddvd_display_quad_count(list) == 1);
    assert(hddvd_display_hit_test(list, 150, 100, &hit) == HDDVD_OK && hit == button_index);
    hddvd_display_list_close(list);
    hddvd_image_cache_destroy(cache);
    hddvd_scene_close(scene);

    /* Without a callback no image is drawn; a callback error fails the build. */
    assert(hddvd_scene_build(xmu, styles, SIZE_MAX, NULL, NULL, &scene) == HDDVD_OK);
    assert(!find(scene, button_index)->normal_image.identity &&
           !find(scene, button_index)->focused);
    hddvd_scene_close(scene);
    lookup.fail = 1;
    scene = (hddvd_scene *)xmu;
    assert(hddvd_scene_build(xmu, styles, SIZE_MAX, image, &lookup, &scene) == HDDVD_EIO && !scene);

    assert(hddvd_scene_build(NULL, styles, 0, NULL, NULL, &scene) == HDDVD_EINVAL);
    assert(hddvd_scene_build(xmu, NULL, 0, NULL, NULL, &scene) == HDDVD_EINVAL);
    assert(hddvd_scene_build(xmu, styles, 0, NULL, NULL, NULL) == HDDVD_EINVAL);
    assert(hddvd_scene_node_count(NULL) == 0 && hddvd_scene_nodes(NULL) == NULL);
    hddvd_scene_close(NULL);
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
}

static void fragments_and_empty(void)
{
    static const char fragment[] = "<div xmlns='" NS "' xmlns:s='" STYLE "' s:width='20px' "
                                   "s:height='10px'><div id='inner'/></div>";
    static const char no_body[] = "<root xmlns='" NS "'><head/></root>";
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    hddvd_scene *scene = NULL;

    /* A <div> fragment is its own scene, rooted at the fragment. */
    open_markup(fragment, &xmu, &styles);
    assert(hddvd_scene_build(xmu, styles, SIZE_MAX, NULL, NULL, &scene) == HDDVD_OK);
    assert(hddvd_scene_node_count(scene) == 2);
    assert(hddvd_scene_nodes(scene)[0].parent == SIZE_MAX &&
           hddvd_scene_nodes(scene)[1].parent == 0 &&
           hddvd_scene_nodes(scene)[1].bounds.width == 20);
    hddvd_scene_close(scene);
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);

    /* A document without a body draws nothing. */
    open_markup(no_body, &xmu, &styles);
    assert(hddvd_scene_build(xmu, styles, SIZE_MAX, NULL, NULL, &scene) == HDDVD_OK);
    assert(hddvd_scene_node_count(scene) == 0 && !hddvd_scene_nodes(scene));
    hddvd_scene_close(scene);
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
}

int main(void)
{
    builds_scene();
    fragments_and_empty();
    return 0;
}
