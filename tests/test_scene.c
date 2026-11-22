/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
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
    /* "placed..." images are 20x10, everything else 1x1. */
    int placed = !strncmp(id, "placed", 6);
    (void)opaque;
    (void)data;
    (void)size;
    memset(plane, 0, sizeof(*plane));
    plane->width = placed ? 20 : 1;
    plane->height = placed ? 10 : 1;
    plane->stride = plane->width * 4u;
    plane->pixels = calloc(plane->height, plane->stride);
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

/* Background fills, text from <p> and <input state:value>, object images,
   and placed background images, through to display-list items. */
static void decorations_and_items(void)
{
    static const char xml[] =
        "<root xmlns='" NS "' xmlns:s='" STYLE "' xmlns:st='" NS "#state'><body>"
        "<div id='panel' s:width='200px' s:height='100px' s:backgroundColor='#102030' "
        "s:backgroundImage=\"url('placed.png')\" s:backgroundPositionHorizontal='center' "
        "s:backgroundPositionVertical='bottom'>"
        "<p id='title' s:color='rgba(1,2,3,4)' s:font='Original.ttf' s:fontSize='30px' "
        "s:textAlign='center' s:wrapOption='no-wrap'>Hello</p>"
        "<p id='blank'>   </p>"
        "<input id='label' mode='display' st:value='OK' s:width='40px' s:height='20px'/>"
        "<object id='picture' type='image/png' src='picture.png' s:width='8px' "
        "s:height='8px'/>"
        "<object id='sound' type='audio/x-wav' src='sound.wav'/>"
        "<div id='far' s:width='10px' s:height='10px' s:backgroundImage=\"url('placed2.png')\" "
        "s:backgroundPositionHorizontal='500px'/>"
        "<div id='scaled' s:x='50px' s:width='100px' s:height='40px' "
        "s:backgroundImage=\"url('placed3.png')\" s:backgroundPositionHorizontal='25%' "
        "s:contentWidth='50%'/></div></body></root>";
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    hddvd_scene *scene = NULL;
    hddvd_image_cache *cache = NULL;
    hddvd_display_list *list = NULL, *again = NULL;
    hddvd_scene_decoration d;
    hddvd_display_item item;
    hddvd_display_quad quad;
    struct lookup lookup = {0, 0};
    size_t i, panel, title, label, picture, sound, scaled, images = 0;
    uint64_t hit;

    open_markup(xml, &xmu, &styles);
    assert(hddvd_scene_build(xmu, styles, SIZE_MAX, image, &lookup, &scene) == HDDVD_OK);
    panel = (size_t)(find(scene, hddvd_xmu_find_id(xmu, "panel")) - hddvd_scene_nodes(scene));
    title = (size_t)(find(scene, hddvd_xmu_find_id(xmu, "title")) - hddvd_scene_nodes(scene));
    label = (size_t)(find(scene, hddvd_xmu_find_id(xmu, "label")) - hddvd_scene_nodes(scene));
    picture = (size_t)(find(scene, hddvd_xmu_find_id(xmu, "picture")) - hddvd_scene_nodes(scene));
    sound = (size_t)(find(scene, hddvd_xmu_find_id(xmu, "sound")) - hddvd_scene_nodes(scene));
    scaled = (size_t)(find(scene, hddvd_xmu_find_id(xmu, "scaled")) - hddvd_scene_nodes(scene));

    assert(hddvd_scene_get_decoration(scene, panel, &d) == HDDVD_OK);
    assert(d.properties == (HDDVD_SCENE_FILL | HDDVD_SCENE_IMAGE_PLACED) &&
           d.fill_color == 0x102030ffu && d.image_x == HDDVD_XMU_BACKGROUND_CENTER &&
           d.image_y == HDDVD_XMU_BACKGROUND_END);
    assert(hddvd_scene_get_decoration(scene, title, &d) == HDDVD_OK);
    assert(d.properties == (HDDVD_SCENE_TEXT | HDDVD_SCENE_TEXT_COLOR) &&
           !strcmp(d.text, "Hello") && d.text_color == 0x01020304u &&
           !strcmp(d.font, "Original.ttf") && d.font_size == 30 &&
           d.text_align == HDDVD_XMU_ALIGN_CENTER && d.wrap == 0);
    /* Whitespace is not text; an input's state:value is, wrapping by default. */
    assert(hddvd_scene_get_decoration(scene, title + 1, &d) == HDDVD_OK && !d.properties);
    assert(hddvd_scene_get_decoration(scene, label, &d) == HDDVD_OK &&
           d.properties == HDDVD_SCENE_TEXT && !strcmp(d.text, "OK") && d.wrap == 1 && !d.font &&
           !d.font_size);
    /* Image objects draw their src; other objects draw nothing. */
    assert(!strcmp(hddvd_scene_nodes(scene)[picture].normal_image.identity, "picture.png"));
    assert(!hddvd_scene_nodes(scene)[sound].normal_image.identity);
    assert(hddvd_scene_get_decoration(scene, scaled, &d) == HDDVD_OK &&
           d.image_x == HDDVD_XMU_BACKGROUND_PERCENT && d.image_x_offset == 25 &&
           d.image_width_percent == 50);
    assert(hddvd_scene_get_decoration(scene, hddvd_scene_node_count(scene), &d) == HDDVD_EINVAL);

    assert(hddvd_image_cache_create(decode, NULL, NULL, &cache) == HDDVD_OK);
    assert(hddvd_display_build_scene(cache, 1920, 1080, scene, NULL, &list) == HDDVD_OK);
    /* panel: fill, image; title: text; label: text; picture: image; far: its
       image lies outside the node; scaled: image. */
    assert(hddvd_display_item_count(list) == 6);
    assert(hddvd_display_get_item(list, 0, &item) == HDDVD_OK &&
           item.kind == HDDVD_DISPLAY_ITEM_FILL && item.color == 0x102030ffu && item.has_color &&
           item.quad.destination.width == 200);
    /* A 20x10 image centered horizontally, at the bottom of a 200x100 node. */
    assert(hddvd_display_get_item(list, 1, &item) == HDDVD_OK &&
           item.kind == HDDVD_DISPLAY_ITEM_IMAGE && item.quad.destination.x == 90 &&
           item.quad.destination.y == 90 && item.quad.destination.width == 20 &&
           item.quad.destination.height == 10);
    assert(hddvd_display_get_item(list, 2, &item) == HDDVD_OK &&
           item.kind == HDDVD_DISPLAY_ITEM_TEXT && !strcmp(item.text, "Hello") && item.has_color &&
           item.color == 0x01020304u && item.font_size == 30 && !item.wrap);
    assert(hddvd_display_get_item(list, 3, &item) == HDDVD_OK &&
           item.kind == HDDVD_DISPLAY_ITEM_TEXT && !strcmp(item.text, "OK") && !item.has_color &&
           item.quad.destination.width == 40);
    assert(hddvd_display_get_item(list, 4, &item) == HDDVD_OK &&
           item.kind == HDDVD_DISPLAY_ITEM_IMAGE && item.quad.destination.width == 8);
    /* 50 of a 100px width, its 25% point on the node's 25% point. */
    assert(hddvd_display_get_item(list, 5, &item) == HDDVD_OK &&
           item.kind == HDDVD_DISPLAY_ITEM_IMAGE && item.quad.destination.x == 50 + 12 &&
           item.quad.destination.width == 50 && item.quad.destination.height == 10);
    assert(hddvd_display_get_item(list, 6, &item) == HDDVD_EINVAL);
    /* The quad view still lists image items only. */
    for (i = 0; i < hddvd_display_quad_count(list); i++)
        images += hddvd_display_get_quad(list, i, &quad) == HDDVD_OK && quad.image.pixels;
    assert(hddvd_display_quad_count(list) == 3 && images == 3);
    /* The label input is hit-testable through its text item. */
    assert(hddvd_display_hit_test(list, 10, 10, &hit) == HDDVD_OK &&
           hit == hddvd_xmu_find_id(xmu, "label"));

    /* An identical rebuild damages nothing. */
    assert(hddvd_display_build_scene(cache, 1920, 1080, scene, list, &again) == HDDVD_OK);
    assert(hddvd_display_damage_count(again) == 0);
    hddvd_display_list_close(again);
    assert(hddvd_display_build_scene(cache, 1920, 1080, NULL, NULL, &again) == HDDVD_EINVAL &&
           !again);
    assert(hddvd_display_get_item(NULL, 0, &item) == HDDVD_EINVAL &&
           hddvd_display_item_count(NULL) == 0);
    hddvd_display_list_close(list);
    hddvd_image_cache_destroy(cache);
    hddvd_scene_close(scene);
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
}

int main(void)
{
    builds_scene();
    decorations_and_items();
    fragments_and_empty();
    return 0;
}
