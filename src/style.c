#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define STYLE_NS "http://www.dvdforum.org/2005/ihd#style"

enum nav_direction
{
    NAV_LEFT,
    NAV_RIGHT,
    NAV_UP,
    NAV_DOWN,
    NAV_COUNT
};

struct style_value
{
    hddvd_xmu_style public_value;
    char **images;
    char *nav_ids[NAV_COUNT];
};

struct hddvd_xmu_styles
{
    const hddvd_xmu *xmu;
    struct style_value *specified;
    struct style_value *computed;
    size_t *z_order;
    size_t count;
};

static void value_init(struct style_value *value)
{
    memset(value, 0, sizeof(*value));
    value->public_value.nav_left = SIZE_MAX;
    value->public_value.nav_right = SIZE_MAX;
    value->public_value.nav_up = SIZE_MAX;
    value->public_value.nav_down = SIZE_MAX;
}

static void value_clear(struct style_value *value)
{
    size_t i;
    for (i = 0; i < value->public_value.background_image_count; i++)
        free(value->images[i]);
    free(value->images);
    for (i = 0; i < NAV_COUNT; i++)
        free(value->nav_ids[i]);
    value_init(value);
}

static char *duplicate_range(const char *start, size_t length)
{
    char *copy = malloc(length + 1);
    if (copy)
    {
        memcpy(copy, start, length);
        copy[length] = '\0';
    }
    return copy;
}

static int parse_signed(const char *text, int pixels, int32_t *result)
{
    char *end;
    long long value;
    errno = 0;
    value = strtoll(text, &end, 10);
    if (errno || end == text || value < INT32_MIN || value > INT32_MAX)
        return HDDVD_EPARSE;
    if (pixels)
    {
        if (strcmp(end, "px"))
            return HDDVD_EPARSE;
    }
    else if (*end)
        return HDDVD_EPARSE;
    *result = (int32_t)value;
    return HDDVD_OK;
}

static int parse_unsigned_pixels(const char *text, uint32_t *result)
{
    int32_t signed_value;
    int rc = parse_signed(text, 1, &signed_value);
    if (rc || signed_value < 0)
        return HDDVD_EPARSE;
    *result = (uint32_t)signed_value;
    return HDDVD_OK;
}

static int parse_images(struct style_value *style, const char *text)
{
    const char *cursor = text;
    while (*cursor)
    {
        const char *start, *end;
        char quote;
        char **grown;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
            cursor++;
        if (!*cursor)
            break;
        if (strncmp(cursor, "url(", 4))
            return HDDVD_EPARSE;
        cursor += 4;
        quote = *cursor;
        if (quote != '\'' && quote != '"')
            return HDDVD_EPARSE;
        start = ++cursor;
        end = strchr(start, quote);
        if (!end || end == start || end[1] != ')')
            return HDDVD_EPARSE;
        grown = realloc(style->images,
                        (style->public_value.background_image_count + 1) * sizeof(*grown));
        if (!grown)
            return HDDVD_ENOMEM;
        style->images = grown;
        grown[style->public_value.background_image_count] =
            duplicate_range(start, (size_t)(end - start));
        if (!grown[style->public_value.background_image_count])
            return HDDVD_ENOMEM;
        style->public_value.background_image_count++;
        cursor = end + 2;
        if (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n')
            return HDDVD_EPARSE;
    }
    return style->public_value.background_image_count ? HDDVD_OK : HDDVD_EPARSE;
}

static int set_nav(struct style_value *style, enum nav_direction direction, const char *value,
                   uint32_t property)
{
    if (!*value)
        return HDDVD_EPARSE;
    free(style->nav_ids[direction]);
    style->nav_ids[direction] = duplicate_range(value, strlen(value));
    if (!style->nav_ids[direction])
        return HDDVD_ENOMEM;
    style->public_value.properties |= property;
    return HDDVD_OK;
}

static int parse_property(struct style_value *style, const char *name, const char *value)
{
    int rc;
    if (!strcmp(name, "x"))
    {
        rc = parse_signed(value, 1, &style->public_value.x);
        if (!rc)
            style->public_value.properties |= HDDVD_XMU_STYLE_X;
        return rc;
    }
    if (!strcmp(name, "y"))
    {
        rc = parse_signed(value, 1, &style->public_value.y);
        if (!rc)
            style->public_value.properties |= HDDVD_XMU_STYLE_Y;
        return rc;
    }
    if (!strcmp(name, "width"))
    {
        rc = parse_unsigned_pixels(value, &style->public_value.width);
        if (!rc)
            style->public_value.properties |= HDDVD_XMU_STYLE_WIDTH;
        return rc;
    }
    if (!strcmp(name, "height"))
    {
        rc = parse_unsigned_pixels(value, &style->public_value.height);
        if (!rc)
            style->public_value.properties |= HDDVD_XMU_STYLE_HEIGHT;
        return rc;
    }
    if (!strcmp(name, "zIndex"))
    {
        rc = parse_signed(value, 0, &style->public_value.z_index);
        if (!rc)
            style->public_value.properties |= HDDVD_XMU_STYLE_Z_INDEX;
        return rc;
    }
    if (!strcmp(name, "display"))
    {
        if (!strcmp(value, "none"))
            style->public_value.display = HDDVD_XMU_DISPLAY_NONE;
        else if (!strcmp(value, "block"))
            style->public_value.display = HDDVD_XMU_DISPLAY_BLOCK;
        else
            return HDDVD_EPARSE;
        style->public_value.properties |= HDDVD_XMU_STYLE_DISPLAY;
        return HDDVD_OK;
    }
    if (!strcmp(name, "position"))
    {
        if (strcmp(value, "absolute"))
            return HDDVD_EPARSE;
        style->public_value.position_absolute = 1;
        style->public_value.properties |= HDDVD_XMU_STYLE_POSITION;
        return HDDVD_OK;
    }
    if (!strcmp(name, "backgroundImage"))
    {
        size_t i;
        for (i = 0; i < style->public_value.background_image_count; i++)
            free(style->images[i]);
        free(style->images);
        style->images = NULL;
        style->public_value.background_image_count = 0;
        rc = parse_images(style, value);
        if (!rc)
            style->public_value.properties |= HDDVD_XMU_STYLE_BACKGROUND_IMAGE;
        return rc;
    }
    if (!strcmp(name, "contentWidth") || !strcmp(name, "contentHeight"))
    {
        if (strcmp(value, "scale-to-fit"))
            return HDDVD_EPARSE;
        style->public_value.scale_to_fit = 1;
        style->public_value.properties |= HDDVD_XMU_STYLE_SCALE_TO_FIT;
        return HDDVD_OK;
    }
    if (!strcmp(name, "navLeft"))
        return set_nav(style, NAV_LEFT, value, HDDVD_XMU_STYLE_NAV_LEFT);
    if (!strcmp(name, "navRight"))
        return set_nav(style, NAV_RIGHT, value, HDDVD_XMU_STYLE_NAV_RIGHT);
    if (!strcmp(name, "navUp"))
        return set_nav(style, NAV_UP, value, HDDVD_XMU_STYLE_NAV_UP);
    if (!strcmp(name, "navDown"))
        return set_nav(style, NAV_DOWN, value, HDDVD_XMU_STYLE_NAV_DOWN);
    /* Unknown style properties are retained by the XMU DOM and ignored here. */
    return HDDVD_OK;
}

static int parse_direct(const hddvd_xmu *xmu, size_t node_index, struct style_value *result)
{
    hddvd_xmu_node node;
    hddvd_xmu_attribute attribute;
    size_t i;
    int rc;
    value_init(result);
    rc = hddvd_xmu_get_node(xmu, node_index, &node);
    if (rc)
        return rc;
    for (i = 0; i < node.attribute_count; i++)
    {
        rc = hddvd_xmu_get_attribute(xmu, node_index, i, &attribute);
        if (rc)
            return rc;
        if (attribute.namespace_uri && !strcmp(attribute.namespace_uri, STYLE_NS))
        {
            rc = parse_property(result, attribute.name, attribute.value);
            if (rc)
                return rc;
        }
    }
    return HDDVD_OK;
}

static const char *style_reference(const hddvd_xmu *xmu, size_t node_index)
{
    hddvd_xmu_node node;
    hddvd_xmu_attribute attribute;
    size_t i;
    if (hddvd_xmu_get_node(xmu, node_index, &node))
        return NULL;
    for (i = 0; i < node.attribute_count; i++)
        if (!hddvd_xmu_get_attribute(xmu, node_index, i, &attribute) && !attribute.namespace_uri &&
            !strcmp(attribute.name, "style"))
            return attribute.value;
    return NULL;
}

static int overlay(struct style_value *target, const struct style_value *source)
{
    uint32_t p = source->public_value.properties;
    size_t i;
#define COPY_PROPERTY(bit, field)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if (p & (bit))                                                                             \
            target->public_value.field = source->public_value.field;                               \
    } while (0)
    COPY_PROPERTY(HDDVD_XMU_STYLE_X, x);
    COPY_PROPERTY(HDDVD_XMU_STYLE_Y, y);
    COPY_PROPERTY(HDDVD_XMU_STYLE_WIDTH, width);
    COPY_PROPERTY(HDDVD_XMU_STYLE_HEIGHT, height);
    COPY_PROPERTY(HDDVD_XMU_STYLE_DISPLAY, display);
    COPY_PROPERTY(HDDVD_XMU_STYLE_Z_INDEX, z_index);
    COPY_PROPERTY(HDDVD_XMU_STYLE_SCALE_TO_FIT, scale_to_fit);
    COPY_PROPERTY(HDDVD_XMU_STYLE_POSITION, position_absolute);
#undef COPY_PROPERTY
    if (p & HDDVD_XMU_STYLE_BACKGROUND_IMAGE)
    {
        for (i = 0; i < target->public_value.background_image_count; i++)
            free(target->images[i]);
        free(target->images);
        target->images = NULL;
        target->public_value.background_image_count = 0;
        for (i = 0; i < source->public_value.background_image_count; i++)
        {
            char **grown = realloc(target->images, (i + 1) * sizeof(*grown));
            if (!grown)
                return HDDVD_ENOMEM;
            target->images = grown;
            target->images[i] = duplicate_range(source->images[i], strlen(source->images[i]));
            if (!target->images[i])
                return HDDVD_ENOMEM;
            target->public_value.background_image_count++;
        }
    }
    for (i = 0; i < NAV_COUNT; i++)
    {
        uint32_t bit = HDDVD_XMU_STYLE_NAV_LEFT << i;
        if (!(p & bit))
            continue;
        free(target->nav_ids[i]);
        target->nav_ids[i] = duplicate_range(source->nav_ids[i], strlen(source->nav_ids[i]));
        if (!target->nav_ids[i])
            return HDDVD_ENOMEM;
    }
    target->public_value.properties |= p;
    return HDDVD_OK;
}

static int apply_references(hddvd_xmu_styles *styles, size_t node_index, struct style_value *target,
                            unsigned char *visiting)
{
    const char *references = style_reference(styles->xmu, node_index);
    const char *cursor;
    if (!references)
        return HDDVD_OK;
    if (visiting[node_index])
        return HDDVD_EPARSE;
    visiting[node_index] = 1;
    cursor = references;
    while (*cursor)
    {
        const char *start;
        char *id;
        size_t referenced;
        hddvd_xmu_node referenced_node;
        int rc;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
            cursor++;
        if (!*cursor)
            break;
        start = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n')
            cursor++;
        id = duplicate_range(start, (size_t)(cursor - start));
        if (!id)
            return HDDVD_ENOMEM;
        referenced = hddvd_xmu_find_id(styles->xmu, id);
        free(id);
        if (referenced == SIZE_MAX ||
            hddvd_xmu_get_node(styles->xmu, referenced, &referenced_node) ||
            strcmp(referenced_node.name, "style"))
            return HDDVD_EPARSE;
        rc = apply_references(styles, referenced, target, visiting);
        if (!rc)
            rc = overlay(target, &styles->specified[referenced]);
        if (rc)
            return rc;
    }
    visiting[node_index] = 0;
    return HDDVD_OK;
}

static int resolve_navigation(hddvd_xmu_styles *styles, struct style_value *value)
{
    size_t i;
    size_t *destinations[NAV_COUNT] = {&value->public_value.nav_left,
                                       &value->public_value.nav_right, &value->public_value.nav_up,
                                       &value->public_value.nav_down};
    for (i = 0; i < NAV_COUNT; i++)
    {
        if (!value->nav_ids[i])
            continue;
        *destinations[i] = hddvd_xmu_find_id(styles->xmu, value->nav_ids[i]);
        if (*destinations[i] == SIZE_MAX)
            return HDDVD_EPARSE;
    }
    return HDDVD_OK;
}

static void stable_sort_z(hddvd_xmu_styles *styles)
{
    size_t i;
    for (i = 1; i < styles->count; i++)
    {
        size_t item = styles->z_order[i];
        int32_t z = styles->computed[item].public_value.z_index;
        size_t j = i;
        while (j && styles->computed[styles->z_order[j - 1]].public_value.z_index > z)
        {
            styles->z_order[j] = styles->z_order[j - 1];
            j--;
        }
        styles->z_order[j] = item;
    }
}

int hddvd_xmu_styles_open(const hddvd_xmu *xmu, hddvd_xmu_styles **result)
{
    hddvd_xmu_styles *styles;
    unsigned char *visiting;
    size_t i;
    int rc = HDDVD_OK;
    if (!xmu || !result)
        return HDDVD_EINVAL;
    *result = NULL;
    styles = calloc(1, sizeof(*styles));
    if (!styles)
        return HDDVD_ENOMEM;
    styles->xmu = xmu;
    styles->count = hddvd_xmu_node_count(xmu);
    styles->specified = calloc(styles->count, sizeof(*styles->specified));
    styles->computed = calloc(styles->count, sizeof(*styles->computed));
    styles->z_order = malloc(styles->count * sizeof(*styles->z_order));
    visiting = calloc(styles->count, 1);
    if ((styles->count &&
         (!styles->specified || !styles->computed || !styles->z_order || !visiting)))
        rc = HDDVD_ENOMEM;
    for (i = 0; !rc && i < styles->count; i++)
    {
        value_init(&styles->specified[i]);
        value_init(&styles->computed[i]);
        rc = parse_direct(xmu, i, &styles->specified[i]);
        styles->z_order[i] = i;
    }
    for (i = 0; !rc && i < styles->count; i++)
    {
        rc = apply_references(styles, i, &styles->computed[i], visiting);
        if (!rc)
            rc = overlay(&styles->computed[i], &styles->specified[i]);
        if (!rc)
            rc = resolve_navigation(styles, &styles->computed[i]);
    }
    free(visiting);
    if (rc)
        hddvd_xmu_styles_close(styles);
    else
    {
        stable_sort_z(styles);
        *result = styles;
    }
    return rc;
}

void hddvd_xmu_styles_close(hddvd_xmu_styles *styles)
{
    size_t i;
    if (!styles)
        return;
    for (i = 0; styles->specified && i < styles->count; i++)
        value_clear(&styles->specified[i]);
    for (i = 0; styles->computed && i < styles->count; i++)
    {
        value_clear(&styles->computed[i]);
    }
    free(styles->specified);
    free(styles->computed);
    free(styles->z_order);
    free(styles);
}

static int get_style(const hddvd_xmu_styles *styles, size_t node, hddvd_xmu_style *result,
                     int computed)
{
    if (!styles || !result || node >= styles->count)
        return HDDVD_EINVAL;
    *result = computed ? styles->computed[node].public_value : styles->specified[node].public_value;
    return HDDVD_OK;
}

int hddvd_xmu_get_specified_style(const hddvd_xmu_styles *styles, size_t node,
                                  hddvd_xmu_style *result)
{
    return get_style(styles, node, result, 0);
}

int hddvd_xmu_get_computed_style(const hddvd_xmu_styles *styles, size_t node,
                                 hddvd_xmu_style *result)
{
    return get_style(styles, node, result, 1);
}

const char *hddvd_xmu_style_background_image(const hddvd_xmu_styles *styles, size_t node,
                                             size_t image, int computed)
{
    const struct style_value *value;
    if (!styles || node >= styles->count)
        return NULL;
    value = computed ? &styles->computed[node] : &styles->specified[node];
    return image < value->public_value.background_image_count ? value->images[image] : NULL;
}

const char *hddvd_xmu_style_image_for_state(const hddvd_xmu_styles *styles, size_t node,
                                            int focused)
{
    const struct style_value *value;
    size_t image;
    if (!styles || node >= styles->count)
        return NULL;
    value = &styles->computed[node];
    if (!value->public_value.background_image_count)
        return NULL;
    image = focused && value->public_value.background_image_count > 1 ? 1u : 0u;
    return value->images[image];
}

size_t hddvd_xmu_style_z_order(const hddvd_xmu_styles *styles, size_t position)
{
    return styles && position < styles->count ? styles->z_order[position] : SIZE_MAX;
}
