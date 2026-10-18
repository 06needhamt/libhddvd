#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define STYLE_NS "http://www.dvdforum.org/2005/ihd#style"

/* The first four directions are in hddvd_xmu_style, the diagonals in the
   details record. */
enum nav_direction
{
    NAV_LEFT,
    NAV_RIGHT,
    NAV_UP,
    NAV_DOWN,
    NAV_LEFT_UP,
    NAV_LEFT_DOWN,
    NAV_RIGHT_UP,
    NAV_RIGHT_DOWN,
    NAV_COUNT
};
#define NAV_PUBLIC 4

/* parse_property results beyond hddvd_status codes. */
#define STYLE_UNKNOWN_PROPERTY 1
#define STYLE_UNKNOWN_KEYWORD 2

struct style_value
{
    hddvd_xmu_style public_value;
    hddvd_xmu_style_details details;
    char **images;
    char *nav_ids[NAV_COUNT];
    char *font;
    char *anchor;
    char *border;
};

struct hddvd_xmu_styles
{
    const hddvd_xmu *xmu;
    struct style_value *specified;
    struct style_value *computed;
    size_t *z_order;
    size_t count;
    int fragment;
    hddvd_xmu_style_unmodeled *unmodeled;
    size_t unmodeled_count;
};

/* Animation keyframes inside <timing> are not static style declarations. */
static int in_timing(const hddvd_xmu *xmu, size_t index)
{
    hddvd_xmu_node node;
    if (hddvd_xmu_get_node(xmu, index, &node))
        return 0;
    while (node.parent != SIZE_MAX)
    {
        index = node.parent;
        if (hddvd_xmu_get_node(xmu, index, &node))
            return 0;
        if (!strcmp(node.name, "timing"))
            return 1;
    }
    return 0;
}

static void value_init(struct style_value *value)
{
    memset(value, 0, sizeof(*value));
    value->public_value.nav_left = SIZE_MAX;
    value->public_value.nav_right = SIZE_MAX;
    value->public_value.nav_up = SIZE_MAX;
    value->public_value.nav_down = SIZE_MAX;
    value->details.nav_left_up = SIZE_MAX;
    value->details.nav_left_down = SIZE_MAX;
    value->details.nav_right_up = SIZE_MAX;
    value->details.nav_right_down = SIZE_MAX;
}

static void value_clear(struct style_value *value)
{
    size_t i;
    for (i = 0; i < value->public_value.background_image_count; i++)
        free(value->images[i]);
    free(value->images);
    for (i = 0; i < NAV_COUNT; i++)
        free(value->nav_ids[i]);
    free(value->font);
    free(value->anchor);
    free(value->border);
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

static int parse_unsigned_pixels(const char *text, uint32_t *result);

/* A whole, non-negative percentage such as "100%". */
static int parse_percent(const char *text, uint32_t *result)
{
    char *end;
    unsigned long value;
    if (*text < '0' || *text > '9')
        return HDDVD_EPARSE;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || strcmp(end, "%") || value > UINT32_MAX)
        return HDDVD_EPARSE;
    *result = (uint32_t)value;
    return HDDVD_OK;
}

static int parse_length(const char *text, uint32_t *result, int *percent)
{
    size_t length = strlen(text);
    *percent = length && text[length - 1] == '%';
    return *percent ? parse_percent(text, result) : parse_unsigned_pixels(text, result);
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
                   uint32_t *properties, uint32_t property)
{
    if (!*value)
        return HDDVD_EPARSE;
    free(style->nav_ids[direction]);
    style->nav_ids[direction] = NULL;
    /* "none" explicitly removes navigation, overriding referenced styles. */
    if (strcmp(value, "none"))
    {
        style->nav_ids[direction] = duplicate_range(value, strlen(value));
        if (!style->nav_ids[direction])
            return HDDVD_ENOMEM;
    }
    *properties |= property;
    return HDDVD_OK;
}

static int set_string(char **slot, const char *value)
{
    if (!*value)
        return HDDVD_EPARSE;
    free(*slot);
    *slot = duplicate_range(value, strlen(value));
    return *slot ? HDDVD_OK : HDDVD_ENOMEM;
}

/* opacity: a decimal from 0 to 1, kept in thousandths. */
static int parse_opacity(const char *text, uint16_t *result)
{
    unsigned whole, fraction = 0, scale = 100;
    if (*text < '0' || *text > '9')
        return HDDVD_EPARSE;
    whole = (unsigned)(*text++ - '0');
    if (*text == '.')
    {
        text++;
        if (*text < '0' || *text > '9')
            return HDDVD_EPARSE;
        for (; *text >= '0' && *text <= '9'; text++, scale /= 10)
            fraction += scale * (unsigned)(*text - '0');
    }
    if (*text || whole > 1 || (whole == 1 && fraction))
        return HDDVD_EPARSE;
    *result = (uint16_t)(whole * 1000u + fraction);
    return HDDVD_OK;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* rgb(r,g,b) or rgba(r,g,b,a) with components from 0 to 255. */
static int parse_components(const char *text, unsigned count, uint32_t *result)
{
    uint32_t value = 0;
    unsigned i;
    for (i = 0; i < count; i++)
    {
        char *end;
        unsigned long component;
        while (*text == ' ')
            text++;
        if (*text < '0' || *text > '9')
            return HDDVD_EPARSE;
        errno = 0;
        component = strtoul(text, &end, 10);
        if (errno || component > 255)
            return HDDVD_EPARSE;
        text = end;
        while (*text == ' ')
            text++;
        if (*text != (i + 1 == count ? ')' : ','))
            return HDDVD_EPARSE;
        text++;
        value = value << 8 | (uint32_t)component;
    }
    if (*text)
        return HDDVD_EPARSE;
    *result = count == 3 ? value << 8 | 0xffu : value;
    return HDDVD_OK;
}

static int parse_color(const char *text, uint32_t *result)
{
    static const struct
    {
        const char *name;
        uint32_t rgba;
    } names[] = {{"black", 0x000000ffu},  {"silver", 0xc0c0c0ffu}, {"gray", 0x808080ffu},
                 {"grey", 0x808080ffu},   {"white", 0xffffffffu},  {"maroon", 0x800000ffu},
                 {"red", 0xff0000ffu},    {"purple", 0x800080ffu}, {"fuchsia", 0xff00ffffu},
                 {"green", 0x008000ffu},  {"lime", 0x00ff00ffu},   {"olive", 0x808000ffu},
                 {"yellow", 0xffff00ffu}, {"navy", 0x000080ffu},   {"blue", 0x0000ffffu},
                 {"teal", 0x008080ffu},   {"aqua", 0x00ffffffu},   {"transparent", 0}};
    size_t i;
    if (*text == '#')
    {
        uint32_t value = 0;
        for (i = 1; i <= 6; i++)
        {
            int digit = hex_digit(text[i]);
            if (digit < 0)
                return HDDVD_EPARSE;
            value = value << 4 | (uint32_t)digit;
        }
        if (text[7])
            return HDDVD_EPARSE;
        *result = value << 8 | 0xffu;
        return HDDVD_OK;
    }
    if (!strncmp(text, "rgba(", 5))
        return parse_components(text + 5, 4, result);
    if (!strncmp(text, "rgb(", 4))
        return parse_components(text + 4, 3, result);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(text, names[i].name))
        {
            *result = names[i].rgba;
            return HDDVD_OK;
        }
    return STYLE_UNKNOWN_KEYWORD;
}

/* Matches value against a keyword list; the keyword's position is the
   enum value. */
static int keyword(const char *value, const char *const *keywords, unsigned *result)
{
    unsigned i;
    for (i = 0; keywords[i]; i++)
        if (!strcmp(value, keywords[i]))
        {
            *result = i;
            return HDDVD_OK;
        }
    return STYLE_UNKNOWN_KEYWORD;
}

static int parse_unsigned(const char *text, uint32_t *result)
{
    int32_t value;
    if (*text < '0' || *text > '9' || parse_signed(text, 0, &value) || value < 0)
        return HDDVD_EPARSE;
    *result = (uint32_t)value;
    return HDDVD_OK;
}

/* A value that does not start like a number is a keyword, reported rather
   than rejected; one that does must parse. */
static int numeric(const char *value, int (*parse)(const char *, uint32_t *), uint32_t *result)
{
    if ((*value < '0' || *value > '9') && *value != '-' && *value != '+' && *value != '.')
        return STYLE_UNKNOWN_KEYWORD;
    return parse(value, result);
}

static int parse_background_position(const char *value, const char *start, const char *end,
                                     hddvd_xmu_background_position *position, int32_t *offset)
{
    if (!strcmp(value, start))
        *position = HDDVD_XMU_BACKGROUND_START;
    else if (!strcmp(value, "center"))
        *position = HDDVD_XMU_BACKGROUND_CENTER;
    else if (!strcmp(value, end))
        *position = HDDVD_XMU_BACKGROUND_END;
    else if (*value == '-' || (*value >= '0' && *value <= '9'))
    {
        uint32_t percent;
        size_t length = strlen(value);
        if (value[length - 1] != '%')
        {
            *position = HDDVD_XMU_BACKGROUND_OFFSET;
            return parse_signed(value, 1, offset);
        }
        if (parse_percent(value, &percent) || percent > INT32_MAX)
            return HDDVD_EPARSE;
        *position = HDDVD_XMU_BACKGROUND_PERCENT;
        *offset = (int32_t)percent;
        return HDDVD_OK;
    }
    else
        return STYLE_UNKNOWN_KEYWORD;
    *offset = 0;
    return HDDVD_OK;
}

static int parse_crop(const char *text, int32_t crop[4])
{
    unsigned i;
    for (i = 0; i < 4; i++)
    {
        char *end;
        long long value;
        while (*text == ' ')
            text++;
        errno = 0;
        value = strtoll(text, &end, 10);
        if (errno || end == text || value < INT32_MIN || value > INT32_MAX)
            return HDDVD_EPARSE;
        crop[i] = (int32_t)value;
        text = end;
    }
    while (*text == ' ')
        text++;
    return *text ? HDDVD_EPARSE : HDDVD_OK;
}

/* Properties of the details record. Returns STYLE_UNKNOWN_PROPERTY for a
   name it does not know. */
static int parse_detail(struct style_value *style, const char *name, const char *value)
{
    static const char *const visibility[] = {"visible", "hidden", "inherit", NULL};
    static const char *const font_style[] = {"normal", "italic", "oblique", NULL};
    static const char *const text_align[] = {"start", "center", "end", "left", "right", NULL};
    static const char *const display_align[] = {"before", "center", "after", NULL};
    static const char *const wrap[] = {"no-wrap", "wrap", NULL};
    static const char *const repeat[] = {"no-repeat", "repeat", NULL};
    static const struct
    {
        const char *name;
        enum nav_direction direction;
        uint32_t bit;
    } diagonals[] = {{"navLeftUp", NAV_LEFT_UP, HDDVD_XMU_DETAIL_NAV_LEFT_UP},
                     {"navLeftDown", NAV_LEFT_DOWN, HDDVD_XMU_DETAIL_NAV_LEFT_DOWN},
                     {"navRightUp", NAV_RIGHT_UP, HDDVD_XMU_DETAIL_NAV_RIGHT_UP},
                     {"navRightDown", NAV_RIGHT_DOWN, HDDVD_XMU_DETAIL_NAV_RIGHT_DOWN}};
    hddvd_xmu_style_details *d = &style->details;
    uint32_t bit;
    unsigned choice = 0;
    size_t i;
    int rc;
    for (i = 0; i < sizeof(diagonals) / sizeof(diagonals[0]); i++)
        if (!strcmp(name, diagonals[i].name))
            return set_nav(style, diagonals[i].direction, value, &d->properties, diagonals[i].bit);
    if (!strcmp(name, "opacity"))
    {
        bit = HDDVD_XMU_DETAIL_OPACITY;
        d->opacity_inherit = !strcmp(value, "inherit");
        if (d->opacity_inherit)
            rc = HDDVD_OK;
        else if ((*value < '0' || *value > '9') && *value != '.' && *value != '-')
            rc = STYLE_UNKNOWN_KEYWORD;
        else
            rc = parse_opacity(value, &d->opacity);
    }
    else if (!strcmp(name, "visibility"))
    {
        bit = HDDVD_XMU_DETAIL_VISIBILITY;
        rc = keyword(value, visibility, &choice);
        d->visibility = (hddvd_xmu_visibility)choice;
    }
    else if (!strcmp(name, "color"))
    {
        bit = HDDVD_XMU_DETAIL_COLOR;
        rc = parse_color(value, &d->color);
    }
    else if (!strcmp(name, "backgroundColor"))
    {
        bit = HDDVD_XMU_DETAIL_BACKGROUND_COLOR;
        rc = parse_color(value, &d->background_color);
    }
    else if (!strcmp(name, "font"))
    {
        bit = HDDVD_XMU_DETAIL_FONT;
        rc = set_string(&style->font, value);
    }
    else if (!strcmp(name, "fontSize"))
    {
        bit = HDDVD_XMU_DETAIL_FONT_SIZE;
        rc = numeric(value, parse_unsigned_pixels, &d->font_size);
    }
    else if (!strcmp(name, "fontStyle"))
    {
        bit = HDDVD_XMU_DETAIL_FONT_STYLE;
        rc = keyword(value, font_style, &choice);
        d->font_style = (hddvd_xmu_font_style)choice;
    }
    else if (!strcmp(name, "textAlign"))
    {
        bit = HDDVD_XMU_DETAIL_TEXT_ALIGN;
        rc = keyword(value, text_align, &choice);
        d->text_align = (hddvd_xmu_align)choice;
    }
    else if (!strcmp(name, "displayAlign"))
    {
        bit = HDDVD_XMU_DETAIL_DISPLAY_ALIGN;
        rc = keyword(value, display_align, &choice);
        d->display_align = (hddvd_xmu_align)choice;
    }
    else if (!strcmp(name, "wrapOption"))
    {
        bit = HDDVD_XMU_DETAIL_WRAP;
        rc = keyword(value, wrap, &choice);
        d->wrap = (uint8_t)choice;
    }
    else if (!strcmp(name, "textAltitude"))
    {
        bit = HDDVD_XMU_DETAIL_TEXT_ALTITUDE;
        rc = numeric(value, parse_percent, &d->text_altitude_percent);
    }
    else if (!strcmp(name, "anchor"))
    {
        bit = HDDVD_XMU_DETAIL_ANCHOR;
        rc = set_string(&style->anchor, value);
    }
    else if (!strcmp(name, "border"))
    {
        bit = HDDVD_XMU_DETAIL_BORDER;
        rc = set_string(&style->border, value);
    }
    else if (!strcmp(name, "padding"))
    {
        /* Only the single-length form has been observed. */
        bit = HDDVD_XMU_DETAIL_PADDING_BEFORE | HDDVD_XMU_DETAIL_PADDING_AFTER |
              HDDVD_XMU_DETAIL_PADDING_START | HDDVD_XMU_DETAIL_PADDING_END;
        rc = numeric(value, parse_unsigned_pixels, &d->padding_before);
        d->padding_after = d->padding_start = d->padding_end = d->padding_before;
    }
    else if (!strcmp(name, "paddingBefore"))
    {
        bit = HDDVD_XMU_DETAIL_PADDING_BEFORE;
        rc = numeric(value, parse_unsigned_pixels, &d->padding_before);
    }
    else if (!strcmp(name, "paddingAfter"))
    {
        bit = HDDVD_XMU_DETAIL_PADDING_AFTER;
        rc = numeric(value, parse_unsigned_pixels, &d->padding_after);
    }
    else if (!strcmp(name, "paddingStart"))
    {
        bit = HDDVD_XMU_DETAIL_PADDING_START;
        rc = numeric(value, parse_unsigned_pixels, &d->padding_start);
    }
    else if (!strcmp(name, "paddingEnd"))
    {
        bit = HDDVD_XMU_DETAIL_PADDING_END;
        rc = numeric(value, parse_unsigned_pixels, &d->padding_end);
    }
    else if (!strcmp(name, "backgroundPositionHorizontal"))
    {
        bit = HDDVD_XMU_DETAIL_BACKGROUND_X;
        rc = parse_background_position(value, "left", "right", &d->background_x,
                                       &d->background_x_offset);
    }
    else if (!strcmp(name, "backgroundPositionVertical"))
    {
        bit = HDDVD_XMU_DETAIL_BACKGROUND_Y;
        rc = parse_background_position(value, "top", "bottom", &d->background_y,
                                       &d->background_y_offset);
    }
    else if (!strcmp(name, "backgroundRepeat"))
    {
        bit = HDDVD_XMU_DETAIL_BACKGROUND_REPEAT;
        rc = keyword(value, repeat, &choice);
        d->background_repeat = (uint8_t)choice;
    }
    else if (!strcmp(name, "backgroundFrame"))
    {
        bit = HDDVD_XMU_DETAIL_BACKGROUND_FRAME;
        rc = numeric(value, parse_unsigned, &d->background_frame);
    }
    else if (!strcmp(name, "crop"))
    {
        bit = HDDVD_XMU_DETAIL_CROP;
        rc = parse_crop(value, d->crop);
    }
    else if (!strcmp(name, "navIndex"))
    {
        bit = HDDVD_XMU_DETAIL_NAV_INDEX;
        d->nav_index_none = !strcmp(value, "none");
        rc = d->nav_index_none ? HDDVD_OK : numeric(value, parse_unsigned, &d->nav_index);
    }
    else
        return STYLE_UNKNOWN_PROPERTY;
    if (!rc)
        d->properties |= bit;
    return rc;
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
        int percent;
        rc = parse_length(value, &style->public_value.width, &percent);
        if (!rc)
        {
            style->public_value.properties &=
                ~(uint32_t)(HDDVD_XMU_STYLE_WIDTH | HDDVD_XMU_STYLE_WIDTH_PERCENT);
            style->public_value.properties |=
                percent ? HDDVD_XMU_STYLE_WIDTH_PERCENT : HDDVD_XMU_STYLE_WIDTH;
        }
        return rc;
    }
    if (!strcmp(name, "height"))
    {
        int percent;
        rc = parse_length(value, &style->public_value.height, &percent);
        if (!rc)
        {
            style->public_value.properties &=
                ~(uint32_t)(HDDVD_XMU_STYLE_HEIGHT | HDDVD_XMU_STYLE_HEIGHT_PERCENT);
            style->public_value.properties |=
                percent ? HDDVD_XMU_STYLE_HEIGHT_PERCENT : HDDVD_XMU_STYLE_HEIGHT;
        }
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
        else if (!strcmp(value, "auto"))
            style->public_value.display = HDDVD_XMU_DISPLAY_AUTO;
        else
            return HDDVD_EPARSE;
        style->public_value.properties |= HDDVD_XMU_STYLE_DISPLAY;
        return HDDVD_OK;
    }
    if (!strcmp(name, "position"))
    {
        if (strcmp(value, "absolute") && strcmp(value, "relative"))
            return HDDVD_EPARSE;
        style->public_value.position_absolute = !strcmp(value, "absolute");
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
        int width = !strcmp(name, "contentWidth");
        uint32_t percent;
        if (parse_percent(value, &percent) == HDDVD_OK)
        {
            if (width)
                style->details.content_width_percent = percent;
            else
                style->details.content_height_percent = percent;
            style->details.properties |=
                width ? HDDVD_XMU_DETAIL_CONTENT_WIDTH : HDDVD_XMU_DETAIL_CONTENT_HEIGHT;
            return HDDVD_OK;
        }
        if (strcmp(value, "scale-to-fit"))
            return HDDVD_EPARSE;
        style->public_value.scale_to_fit = 1;
        style->public_value.properties |= HDDVD_XMU_STYLE_SCALE_TO_FIT;
        return HDDVD_OK;
    }
    if (!strcmp(name, "navLeft"))
        return set_nav(style, NAV_LEFT, value, &style->public_value.properties,
                       HDDVD_XMU_STYLE_NAV_LEFT);
    if (!strcmp(name, "navRight"))
        return set_nav(style, NAV_RIGHT, value, &style->public_value.properties,
                       HDDVD_XMU_STYLE_NAV_RIGHT);
    if (!strcmp(name, "navUp"))
        return set_nav(style, NAV_UP, value, &style->public_value.properties,
                       HDDVD_XMU_STYLE_NAV_UP);
    if (!strcmp(name, "navDown"))
        return set_nav(style, NAV_DOWN, value, &style->public_value.properties,
                       HDDVD_XMU_STYLE_NAV_DOWN);
    /* Everything else is a detail property, or reported as unmodeled. */
    return parse_detail(style, name, value);
}

static int note_unmodeled(hddvd_xmu_styles *styles, const char *property, const char *value)
{
    hddvd_xmu_style_unmodeled *grown, *record;
    size_t i;
    for (i = 0; i < styles->unmodeled_count; i++)
    {
        record = &styles->unmodeled[i];
        if (!strcmp(record->property, property) &&
            (value ? record->value && !strcmp(record->value, value) : !record->value))
        {
            record->occurrences++;
            return HDDVD_OK;
        }
    }
    grown = realloc(styles->unmodeled, (styles->unmodeled_count + 1) * sizeof(*grown));
    if (!grown)
        return HDDVD_ENOMEM;
    styles->unmodeled = grown;
    record = &grown[styles->unmodeled_count];
    record->property = duplicate_range(property, strlen(property));
    record->value = value ? duplicate_range(value, strlen(value)) : NULL;
    record->occurrences = 1;
    if (!record->property || (value && !record->value))
    {
        free((char *)record->property);
        free((char *)record->value);
        return HDDVD_ENOMEM;
    }
    styles->unmodeled_count++;
    return HDDVD_OK;
}

static int parse_direct(hddvd_xmu_styles *styles, size_t node_index, struct style_value *result)
{
    const hddvd_xmu *xmu = styles->xmu;
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
            if (rc == STYLE_UNKNOWN_PROPERTY)
                rc = note_unmodeled(styles, attribute.name, NULL);
            else if (rc == STYLE_UNKNOWN_KEYWORD)
                rc = note_unmodeled(styles, attribute.name, attribute.value);
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

static int copy_string(char **target, const char *source)
{
    free(*target);
    *target = source ? duplicate_range(source, strlen(source)) : NULL;
    return source && !*target ? HDDVD_ENOMEM : HDDVD_OK;
}

static int overlay_details(struct style_value *target, const struct style_value *source)
{
    const hddvd_xmu_style_details *s = &source->details;
    hddvd_xmu_style_details *t = &target->details;
    uint32_t p = s->properties;
    int rc = HDDVD_OK;
#define COPY_DETAIL(bit, field)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if (p & (bit))                                                                             \
            t->field = s->field;                                                                   \
    } while (0)
    COPY_DETAIL(HDDVD_XMU_DETAIL_OPACITY, opacity);
    COPY_DETAIL(HDDVD_XMU_DETAIL_OPACITY, opacity_inherit);
    COPY_DETAIL(HDDVD_XMU_DETAIL_VISIBILITY, visibility);
    COPY_DETAIL(HDDVD_XMU_DETAIL_COLOR, color);
    COPY_DETAIL(HDDVD_XMU_DETAIL_BACKGROUND_COLOR, background_color);
    COPY_DETAIL(HDDVD_XMU_DETAIL_FONT_SIZE, font_size);
    COPY_DETAIL(HDDVD_XMU_DETAIL_FONT_STYLE, font_style);
    COPY_DETAIL(HDDVD_XMU_DETAIL_TEXT_ALIGN, text_align);
    COPY_DETAIL(HDDVD_XMU_DETAIL_DISPLAY_ALIGN, display_align);
    COPY_DETAIL(HDDVD_XMU_DETAIL_WRAP, wrap);
    COPY_DETAIL(HDDVD_XMU_DETAIL_TEXT_ALTITUDE, text_altitude_percent);
    COPY_DETAIL(HDDVD_XMU_DETAIL_PADDING_BEFORE, padding_before);
    COPY_DETAIL(HDDVD_XMU_DETAIL_PADDING_AFTER, padding_after);
    COPY_DETAIL(HDDVD_XMU_DETAIL_PADDING_START, padding_start);
    COPY_DETAIL(HDDVD_XMU_DETAIL_PADDING_END, padding_end);
    COPY_DETAIL(HDDVD_XMU_DETAIL_BACKGROUND_X, background_x);
    COPY_DETAIL(HDDVD_XMU_DETAIL_BACKGROUND_X, background_x_offset);
    COPY_DETAIL(HDDVD_XMU_DETAIL_BACKGROUND_Y, background_y);
    COPY_DETAIL(HDDVD_XMU_DETAIL_BACKGROUND_Y, background_y_offset);
    COPY_DETAIL(HDDVD_XMU_DETAIL_BACKGROUND_REPEAT, background_repeat);
    COPY_DETAIL(HDDVD_XMU_DETAIL_BACKGROUND_FRAME, background_frame);
    COPY_DETAIL(HDDVD_XMU_DETAIL_CONTENT_WIDTH, content_width_percent);
    COPY_DETAIL(HDDVD_XMU_DETAIL_CONTENT_HEIGHT, content_height_percent);
    COPY_DETAIL(HDDVD_XMU_DETAIL_NAV_INDEX, nav_index);
    COPY_DETAIL(HDDVD_XMU_DETAIL_NAV_INDEX, nav_index_none);
#undef COPY_DETAIL
    if (p & HDDVD_XMU_DETAIL_CROP)
        memcpy(t->crop, s->crop, sizeof(t->crop));
    if (p & HDDVD_XMU_DETAIL_FONT)
        rc = copy_string(&target->font, source->font);
    if (!rc && (p & HDDVD_XMU_DETAIL_ANCHOR))
        rc = copy_string(&target->anchor, source->anchor);
    if (!rc && (p & HDDVD_XMU_DETAIL_BORDER))
        rc = copy_string(&target->border, source->border);
    t->properties |= p;
    return rc;
}

static int overlay(struct style_value *target, const struct style_value *source)
{
    uint32_t p = source->public_value.properties;
    size_t i;
    int rc;
#define COPY_PROPERTY(bit, field)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if (p & (bit))                                                                             \
            target->public_value.field = source->public_value.field;                               \
    } while (0)
    COPY_PROPERTY(HDDVD_XMU_STYLE_X, x);
    COPY_PROPERTY(HDDVD_XMU_STYLE_Y, y);
    /* Pixel and percentage lengths replace each other. */
    if (p & (HDDVD_XMU_STYLE_WIDTH | HDDVD_XMU_STYLE_WIDTH_PERCENT))
    {
        target->public_value.width = source->public_value.width;
        target->public_value.properties &=
            ~(uint32_t)(HDDVD_XMU_STYLE_WIDTH | HDDVD_XMU_STYLE_WIDTH_PERCENT);
    }
    if (p & (HDDVD_XMU_STYLE_HEIGHT | HDDVD_XMU_STYLE_HEIGHT_PERCENT))
    {
        target->public_value.height = source->public_value.height;
        target->public_value.properties &=
            ~(uint32_t)(HDDVD_XMU_STYLE_HEIGHT | HDDVD_XMU_STYLE_HEIGHT_PERCENT);
    }
    COPY_PROPERTY(HDDVD_XMU_STYLE_DISPLAY, display);
    COPY_PROPERTY(HDDVD_XMU_STYLE_Z_INDEX, z_index);
    COPY_PROPERTY(HDDVD_XMU_STYLE_SCALE_TO_FIT, scale_to_fit);
    COPY_PROPERTY(HDDVD_XMU_STYLE_POSITION, position_absolute);
#undef COPY_PROPERTY
    rc = overlay_details(target, source);
    if (rc)
        return rc;
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
        static const uint32_t diagonal_bits[] = {
            HDDVD_XMU_DETAIL_NAV_LEFT_UP, HDDVD_XMU_DETAIL_NAV_LEFT_DOWN,
            HDDVD_XMU_DETAIL_NAV_RIGHT_UP, HDDVD_XMU_DETAIL_NAV_RIGHT_DOWN};
        int set = i < NAV_PUBLIC
                      ? (p & ((uint32_t)HDDVD_XMU_STYLE_NAV_LEFT << i)) != 0
                      : (source->details.properties & diagonal_bits[i - NAV_PUBLIC]) != 0;
        if (!set)
            continue;
        free(target->nav_ids[i]);
        target->nav_ids[i] = NULL;
        if (!source->nav_ids[i])
            continue;
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
        if (referenced == SIZE_MAX && styles->fragment)
            continue;
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
    size_t *destinations[NAV_COUNT] = {
        &value->public_value.nav_left, &value->public_value.nav_right,
        &value->public_value.nav_up,   &value->public_value.nav_down,
        &value->details.nav_left_up,   &value->details.nav_left_down,
        &value->details.nav_right_up,  &value->details.nav_right_down};
    for (i = 0; i < NAV_COUNT; i++)
    {
        if (!value->nav_ids[i])
            continue;
        *destinations[i] = hddvd_xmu_find_id(styles->xmu, value->nav_ids[i]);
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
    if (!rc && styles->count)
    {
        hddvd_xmu_node root;
        styles->fragment = !hddvd_xmu_get_node(xmu, 0, &root) && !strcmp(root.name, "div");
    }
    for (i = 0; !rc && i < styles->count; i++)
    {
        value_init(&styles->specified[i]);
        value_init(&styles->computed[i]);
        if (!in_timing(xmu, i))
            rc = parse_direct(styles, i, &styles->specified[i]);
        styles->z_order[i] = i;
    }
    for (i = 0; !rc && i < styles->count; i++)
    {
        if (in_timing(xmu, i))
            continue;
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
    for (i = 0; i < styles->unmodeled_count; i++)
    {
        free((char *)styles->unmodeled[i].property);
        free((char *)styles->unmodeled[i].value);
    }
    free(styles->unmodeled);
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

int hddvd_xmu_get_style_details(const hddvd_xmu_styles *styles, size_t node, int computed,
                                hddvd_xmu_style_details *result)
{
    const struct style_value *value;
    if (!styles || !result || node >= styles->count)
        return HDDVD_EINVAL;
    value = computed ? &styles->computed[node] : &styles->specified[node];
    *result = value->details;
    result->font = value->font;
    result->anchor = value->anchor;
    result->border = value->border;
    return HDDVD_OK;
}

size_t hddvd_xmu_styles_unmodeled_count(const hddvd_xmu_styles *styles)
{
    return styles ? styles->unmodeled_count : 0;
}

int hddvd_xmu_styles_get_unmodeled(const hddvd_xmu_styles *styles, size_t index,
                                   hddvd_xmu_style_unmodeled *result)
{
    if (!styles || !result || index >= styles->unmodeled_count)
        return HDDVD_EINVAL;
    *result = styles->unmodeled[index];
    return HDDVD_OK;
}

/* Script overrides (runtime.c). The runtime owns `styles` and keeps an
   untouched copy opened from the same markup in `pristine`. */

/* Orders by zIndex, then document order, from the current (nearly sorted)
   order. */
static void sort_z(hddvd_xmu_styles *styles)
{
    size_t i;
    for (i = 1; i < styles->count; i++)
    {
        size_t item = styles->z_order[i];
        int32_t z = styles->computed[item].public_value.z_index;
        size_t j = i;
        while (j)
        {
            size_t before = styles->z_order[j - 1];
            int32_t bz = styles->computed[before].public_value.z_index;
            if (bz < z || (bz == z && before < item))
                break;
            styles->z_order[j] = before;
            j--;
        }
        styles->z_order[j] = item;
    }
}

int hddvd_style_override(hddvd_xmu_styles *styles, size_t node, const char *name, const char *value)
{
    struct style_value scratch;
    int rc;
    if (!styles || !name || !value || node >= styles->count)
        return HDDVD_EINVAL;
    if (in_timing(styles->xmu, node))
        return HDDVD_ENOTSUP;
    /* Validate first, so a rejected value leaves the node untouched. */
    value_init(&scratch);
    rc = parse_property(&scratch, name, value);
    value_clear(&scratch);
    if (rc == STYLE_UNKNOWN_PROPERTY || rc == STYLE_UNKNOWN_KEYWORD)
        return HDDVD_ENOTSUP;
    if (rc)
        return rc;
    rc = parse_property(&styles->computed[node], name, value);
    if (!rc)
        rc = resolve_navigation(styles, &styles->computed[node]);
    if (!rc && !strcmp(name, "zIndex"))
        sort_z(styles);
    return rc;
}

int hddvd_style_restore(hddvd_xmu_styles *styles, const hddvd_xmu_styles *pristine, size_t node)
{
    struct style_value fresh;
    int rc;
    if (!styles || !pristine || node >= styles->count || pristine->count != styles->count)
        return HDDVD_EINVAL;
    value_init(&fresh);
    rc = overlay(&fresh, &pristine->computed[node]);
    if (!rc)
        rc = resolve_navigation(styles, &fresh);
    if (rc)
    {
        value_clear(&fresh);
        return rc;
    }
    value_clear(&styles->computed[node]);
    styles->computed[node] = fresh;
    sort_z(styles);
    return HDDVD_OK;
}

static const char *own_declaration(const hddvd_xmu *xmu, size_t node, const char *name)
{
    hddvd_xmu_node n;
    hddvd_xmu_attribute attribute;
    size_t i;
    if (hddvd_xmu_get_node(xmu, node, &n))
        return NULL;
    for (i = 0; i < n.attribute_count; i++)
        if (!hddvd_xmu_get_attribute(xmu, node, i, &attribute) && attribute.namespace_uri &&
            !strcmp(attribute.namespace_uri, STYLE_NS) && !strcmp(attribute.name, name))
            return attribute.value;
    return NULL;
}

/* Computed values overlay a node's references in order, each after its own
   references, and then the node itself; so the last declaration in that
   order is the node's own, else the latest reference's. Each node's answer
   is kept in `memo` (state 0 unvisited, 1 in progress, 2 done), so a style
   referenced along many paths is searched once. */
struct declared_memo
{
    unsigned char *state;
    const char **value;
};

static const char *declared(const hddvd_xmu_styles *styles, size_t node, const char *name,
                            struct declared_memo *memo)
{
    const char *references, *cursor, *found;
    if (memo->state[node])
        return memo->value[node];
    memo->state[node] = 1;
    found = own_declaration(styles->xmu, node, name);
    references = found ? NULL : style_reference(styles->xmu, node);
    for (cursor = references; cursor && *cursor;)
    {
        const char *start;
        char *id;
        size_t referenced;
        const char *value;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
            cursor++;
        if (!*cursor)
            break;
        start = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n')
            cursor++;
        id = duplicate_range(start, (size_t)(cursor - start));
        if (!id)
            break;
        referenced = hddvd_xmu_find_id(styles->xmu, id);
        free(id);
        if (referenced == SIZE_MAX)
            continue;
        value = declared(styles, referenced, name, memo);
        if (value)
            found = value;
    }
    memo->state[node] = 2;
    memo->value[node] = found;
    return found;
}

const char *hddvd_style_declared(const hddvd_xmu_styles *styles, size_t node, const char *name)
{
    struct declared_memo memo;
    const char *value;
    if (!styles || !name || node >= styles->count || in_timing(styles->xmu, node))
        return NULL;
    value = own_declaration(styles->xmu, node, name);
    if (value || !style_reference(styles->xmu, node))
        return value;
    memo.state = calloc(styles->count, 1);
    memo.value = calloc(styles->count, sizeof(*memo.value));
    value = memo.state && memo.value ? declared(styles, node, name, &memo) : NULL;
    free(memo.state);
    free(memo.value);
    return value;
}
