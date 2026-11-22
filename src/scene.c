/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define SCENE_WIDTH 1920u
#define SCENE_HEIGHT 1080u
#define STATE_NS "http://www.dvdforum.org/2005/ihd#state"

struct scene_image
{
    const char *url;
    const void *data;
    size_t size;
    int found;
};

struct hddvd_scene
{
    hddvd_scene_node *nodes;
    hddvd_scene_decoration *decorations;
    size_t count;
};

/* Per markup node while building: its scene index (SIZE_MAX when it has
   none), its laid-out size, and its own and rendered (own times every
   ancestor's) opacity in thousandths. */
struct scene_state
{
    size_t scene_index;
    uint32_t width;
    uint32_t height;
    uint32_t opacity;
    uint32_t rendered;
};

static int is_skipped(const char *name)
{
    return !strcmp(name, "param") || !strcmp(name, "include");
}

static int is_interactive(const char *name)
{
    return !strcmp(name, "button") || !strcmp(name, "input");
}

/* An attribute's value by namespace (NULL for none) and name. */
static const char *attribute(const hddvd_xmu *xmu, size_t index, size_t count,
                             const char *namespace_uri, const char *name)
{
    hddvd_xmu_attribute a;
    size_t i;
    for (i = 0; i < count; i++)
        if (!hddvd_xmu_get_attribute(xmu, index, i, &a) && !strcmp(a.name, name) &&
            (namespace_uri ? a.namespace_uri && !strcmp(a.namespace_uri, namespace_uri)
                           : !a.namespace_uri))
            return a.value;
    return NULL;
}

static int has_visible_text(const char *text)
{
    for (; text && *text; text++)
        if (*text != ' ' && *text != '\t' && *text != '\r' && *text != '\n')
            return 1;
    return 0;
}

/* An <object type="image/..."> draws its src. */
static const char *object_image(const hddvd_xmu *xmu, size_t index, const hddvd_xmu_node *node)
{
    const char *type;
    if (strcmp(node->name, "object"))
        return NULL;
    type = attribute(xmu, index, node->attribute_count, NULL, "type");
    if (!type || strncmp(type, "image/", 6))
        return NULL;
    return attribute(xmu, index, node->attribute_count, NULL, "src");
}

static void decorate(const hddvd_xmu *xmu, size_t index, const hddvd_xmu_node *node,
                     const hddvd_xmu_style_details *details, hddvd_scene_decoration *d)
{
    const char *text = NULL;
    memset(d, 0, sizeof(*d));
    if (details->properties & HDDVD_XMU_DETAIL_BACKGROUND_COLOR)
    {
        d->properties |= HDDVD_SCENE_FILL;
        d->fill_color = details->background_color;
    }
    if (!strcmp(node->name, "p"))
        text = node->text;
    else if (!strcmp(node->name, "input"))
        text = attribute(xmu, index, node->attribute_count, STATE_NS, "value");
    if (has_visible_text(text))
    {
        d->properties |= HDDVD_SCENE_TEXT;
        d->text = text;
        d->font = details->font;
        d->font_size = details->properties & HDDVD_XMU_DETAIL_FONT_SIZE ? details->font_size : 0;
        d->font_style = details->font_style;
        d->text_align = details->text_align;
        d->display_align = details->display_align;
        /* Wrapping unless wrapOption says otherwise. */
        d->wrap = details->properties & HDDVD_XMU_DETAIL_WRAP ? details->wrap : 1;
        if (details->properties & HDDVD_XMU_DETAIL_COLOR)
        {
            d->properties |= HDDVD_SCENE_TEXT_COLOR;
            d->text_color = details->color;
        }
    }
    if (details->properties & (HDDVD_XMU_DETAIL_BACKGROUND_X | HDDVD_XMU_DETAIL_BACKGROUND_Y))
    {
        d->properties |= HDDVD_SCENE_IMAGE_PLACED;
        if (details->properties & HDDVD_XMU_DETAIL_BACKGROUND_X)
        {
            d->image_x = details->background_x;
            d->image_x_offset = details->background_x_offset;
        }
        if (details->properties & HDDVD_XMU_DETAIL_BACKGROUND_Y)
        {
            d->image_y = details->background_y;
            d->image_y_offset = details->background_y_offset;
        }
        if (details->properties & HDDVD_XMU_DETAIL_CONTENT_WIDTH)
            d->image_width_percent = details->content_width_percent;
        if (details->properties & HDDVD_XMU_DETAIL_CONTENT_HEIGHT)
            d->image_height_percent = details->content_height_percent;
    }
}

/* Finds or requests an image, so each URL reaches the callback once. */
static int lookup_image(struct scene_image **images, size_t *count, const char *url,
                        hddvd_scene_image_fn fn, void *opaque, hddvd_image_source *out)
{
    struct scene_image *grown, *image = NULL;
    size_t i;
    int rc;
    memset(out, 0, sizeof(*out));
    if (!url)
        return HDDVD_OK;
    for (i = 0; i < *count && !image; i++)
        if (!strcmp((*images)[i].url, url))
            image = &(*images)[i];
    if (!image)
    {
        grown = realloc(*images, (*count + 1) * sizeof(*grown));
        if (!grown)
            return HDDVD_ENOMEM;
        *images = grown;
        image = &grown[(*count)++];
        memset(image, 0, sizeof(*image));
        image->url = url;
        rc = fn ? fn(opaque, url, &image->data, &image->size) : HDDVD_ENOTFOUND;
        if (rc && rc != HDDVD_ENOTFOUND)
            return rc;
        image->found = !rc && image->data;
    }
    if (image->found)
    {
        out->identity = image->url;
        out->encoded = image->data;
        out->encoded_size = image->size;
    }
    return HDDVD_OK;
}

/* A computed length: pixels, a percentage of the parent, or the parent's
   size when undeclared. */
static uint32_t length(uint32_t properties, uint32_t pixel_bit, uint32_t percent_bit,
                       uint32_t value, uint32_t parent)
{
    if (properties & pixel_bit)
        return value;
    if (properties & percent_bit)
        return (uint32_t)((uint64_t)parent * value / 100u);
    return parent;
}

static uint8_t to_alpha(uint64_t thousandths)
{
    return (uint8_t)((thousandths * 255u + 500u) / 1000u);
}

/* The first element of a scene: <body>'s first child, or a fragment root. */
static int scene_root(const hddvd_xmu *xmu, size_t *body)
{
    hddvd_xmu_node node;
    size_t i, count = hddvd_xmu_node_count(xmu);
    *body = SIZE_MAX;
    for (i = 0; i < count; i++)
        if (!hddvd_xmu_get_node(xmu, i, &node) && !strcmp(node.name, "body"))
        {
            *body = i;
            return HDDVD_OK;
        }
    return count && !hddvd_xmu_get_node(xmu, 0, &node) && !strcmp(node.name, "div")
               ? HDDVD_OK
               : HDDVD_ENOTFOUND;
}

/* Whether node lies inside the scene container (body), or anywhere in a
   fragment. */
static int in_scene(const struct scene_state *state, const hddvd_xmu_node *node, size_t body,
                    size_t index, int *top_level)
{
    *top_level = 0;
    if (body == SIZE_MAX)
    {
        *top_level = node->parent == SIZE_MAX;
        return 1;
    }
    if (index == body)
        return 0;
    if (node->parent == body)
    {
        *top_level = 1;
        return 1;
    }
    return node->parent != SIZE_MAX && state[node->parent].scene_index != SIZE_MAX;
}

int hddvd_scene_build(const hddvd_xmu *xmu, const hddvd_xmu_styles *styles, size_t focused,
                      hddvd_scene_image_fn fn, void *opaque, hddvd_scene **out)
{
    hddvd_scene *scene;
    struct scene_state *state = NULL;
    struct scene_image *images = NULL;
    size_t i, count, body, image_count = 0;
    int rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!xmu || !styles)
        return HDDVD_EINVAL;
    count = hddvd_xmu_node_count(xmu);
    scene = calloc(1, sizeof(*scene));
    if (!scene)
        return HDDVD_ENOMEM;
    rc = scene_root(xmu, &body);
    if (rc == HDDVD_ENOTFOUND)
    {
        /* Nothing to draw. */
        *out = scene;
        return HDDVD_OK;
    }
    state = calloc(count ? count : 1, sizeof(*state));
    scene->nodes = calloc(count ? count : 1, sizeof(*scene->nodes));
    scene->decorations = calloc(count ? count : 1, sizeof(*scene->decorations));
    if (!state || !scene->nodes || !scene->decorations)
    {
        rc = HDDVD_ENOMEM;
        goto done;
    }
    for (i = 0; i < count && !rc; i++)
    {
        hddvd_xmu_node node;
        hddvd_xmu_style style;
        hddvd_xmu_style_details details;
        hddvd_scene_node *n;
        uint32_t parent_width = SCENE_WIDTH, parent_height = SCENE_HEIGHT, parent_opacity = 1000u,
                 parent_rendered = 1000u;
        size_t parent_scene = SIZE_MAX;
        int top_level;
        state[i].scene_index = SIZE_MAX;
        rc = hddvd_xmu_get_node(xmu, i, &node);
        if (rc)
            break;
        /* The markup model numbers nodes in document order. */
        if (node.parent != SIZE_MAX && node.parent >= i)
        {
            rc = HDDVD_EPARSE;
            break;
        }
        if (!in_scene(state, &node, body, i, &top_level) || is_skipped(node.name))
            continue;
        if (!top_level)
        {
            parent_scene = state[node.parent].scene_index;
            parent_width = state[node.parent].width;
            parent_height = state[node.parent].height;
            parent_opacity = state[node.parent].opacity;
            parent_rendered = state[node.parent].rendered;
        }
        rc = hddvd_xmu_get_computed_style(styles, i, &style);
        if (!rc)
            rc = hddvd_xmu_get_style_details(styles, i, 1, &details);
        if (rc)
            break;
        state[i].width = length(style.properties, HDDVD_XMU_STYLE_WIDTH,
                                HDDVD_XMU_STYLE_WIDTH_PERCENT, style.width, parent_width);
        state[i].height = length(style.properties, HDDVD_XMU_STYLE_HEIGHT,
                                 HDDVD_XMU_STYLE_HEIGHT_PERCENT, style.height, parent_height);
        if (!(details.properties & HDDVD_XMU_DETAIL_OPACITY))
            state[i].opacity = 1000u;
        else
            state[i].opacity = details.opacity_inherit ? parent_opacity : details.opacity;
        /* Group opacity: an element is drawn at the product of its own and
           every ancestor's opacity. */
        state[i].rendered = (uint32_t)((uint64_t)parent_rendered * state[i].opacity / 1000u);

        n = &scene->nodes[scene->count];
        n->id = i;
        n->parent = parent_scene;
        n->bounds.x = style.properties & HDDVD_XMU_STYLE_X ? style.x : 0;
        n->bounds.y = style.properties & HDDVD_XMU_STYLE_Y ? style.y : 0;
        n->bounds.width = state[i].width;
        n->bounds.height = state[i].height;
        n->z_index = style.z_index;
        n->opacity = to_alpha(state[i].rendered);
        n->visible = (uint8_t)(!((style.properties & HDDVD_XMU_STYLE_DISPLAY) &&
                                 style.display == HDDVD_XMU_DISPLAY_NONE) &&
                               !((details.properties & HDDVD_XMU_DETAIL_VISIBILITY) &&
                                 details.visibility == HDDVD_XMU_VISIBILITY_HIDDEN));
        n->focused = (uint8_t)(i == focused);
        n->hit_test = (uint8_t)is_interactive(node.name);
        {
            const char *normal = hddvd_xmu_style_image_for_state(styles, i, 0);
            const char *focus = hddvd_xmu_style_image_for_state(styles, i, 1);
            if (!normal)
                normal = focus = object_image(xmu, i, &node);
            rc = lookup_image(&images, &image_count, normal, fn, opaque, &n->normal_image);
            if (!rc)
                rc = lookup_image(&images, &image_count, focus, fn, opaque, &n->focused_image);
        }
        decorate(xmu, i, &node, &details, &scene->decorations[scene->count]);
        state[i].scene_index = scene->count++;
    }
done:
    free(state);
    free(images);
    if (rc)
    {
        hddvd_scene_close(scene);
        return rc;
    }
    *out = scene;
    return HDDVD_OK;
}

void hddvd_scene_close(hddvd_scene *scene)
{
    if (!scene)
        return;
    free(scene->nodes);
    free(scene->decorations);
    free(scene);
}

size_t hddvd_scene_node_count(const hddvd_scene *scene)
{
    return scene ? scene->count : 0;
}

const hddvd_scene_node *hddvd_scene_nodes(const hddvd_scene *scene)
{
    return scene && scene->count ? scene->nodes : NULL;
}

int hddvd_scene_get_decoration(const hddvd_scene *scene, size_t index, hddvd_scene_decoration *out)
{
    if (!scene || !out || index >= scene->count)
        return HDDVD_EINVAL;
    *out = scene->decorations[index];
    return HDDVD_OK;
}

const hddvd_scene_decoration *hddvd_scene_decorations_internal(const hddvd_scene *scene)
{
    return scene && scene->count ? scene->decorations : NULL;
}
