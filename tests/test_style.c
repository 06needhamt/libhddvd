/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define NS "http://www.dvdforum.org/2005/ihd"
#define STYLE "http://www.dvdforum.org/2005/ihd#style"

static int open_styles(const char *xml, hddvd_xmu **xmu, hddvd_xmu_styles **styles)
{
    int rc = hddvd_xmu_open(xml, strlen(xml), xmu);
    return rc ? rc : hddvd_xmu_styles_open(*xmu, styles);
}

static int model_works(void)
{
    const char xml[] =
        "<root xmlns=\"" NS "\" xmlns:s=\"" STYLE "\"><head><styling>"
        "<style id=\"base\" s:position=\"absolute\" s:x=\"10px\" s:y=\"-2px\" s:width=\"200px\" "
        "s:height=\"100px\" s:display=\"none\" s:zIndex=\"3\"/>"
        "<style id=\"derived\" style=\"base\" s:x=\"20px\" "
        "s:backgroundImage=\"url('normal.png') url(&quot;focused.png&quot;)\" "
        "s:contentWidth=\"scale-to-fit\" s:navRight=\"second\"/>"
        "</styling></head><body><button id=\"first\" style=\"derived\" s:y=\"8px\"/>"
        "<button id=\"second\" s:zIndex=\"3\"/></body></root>";
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    hddvd_xmu_style specified, computed;
    size_t first, second, first_z = SIZE_MAX, second_z = SIZE_MAX, i;
    int ok = open_styles(xml, &xmu, &styles) == HDDVD_OK;
    first = hddvd_xmu_find_id(xmu, "first");
    second = hddvd_xmu_find_id(xmu, "second");
    ok = ok && hddvd_xmu_get_specified_style(styles, first, &specified) == HDDVD_OK &&
         specified.properties == HDDVD_XMU_STYLE_Y && specified.y == 8;
    ok = ok && hddvd_xmu_get_computed_style(styles, first, &computed) == HDDVD_OK &&
         computed.x == 20 && computed.y == 8 && computed.width == 200 && computed.height == 100 &&
         computed.display == HDDVD_XMU_DISPLAY_NONE && computed.z_index == 3 &&
         computed.scale_to_fit && computed.position_absolute && computed.nav_right == second &&
         computed.background_image_count == 2;
    ok = ok && !strcmp(hddvd_xmu_style_background_image(styles, first, 0, 1), "normal.png") &&
         !strcmp(hddvd_xmu_style_background_image(styles, first, 1, 1), "focused.png") &&
         hddvd_xmu_style_background_image(styles, first, 2, 1) == NULL &&
         !strcmp(hddvd_xmu_style_image_for_state(styles, first, 0), "normal.png") &&
         !strcmp(hddvd_xmu_style_image_for_state(styles, first, 1), "focused.png");
    for (i = 0; i < hddvd_xmu_node_count(xmu); i++)
    {
        if (hddvd_xmu_style_z_order(styles, i) == first)
            first_z = i;
        if (hddvd_xmu_style_z_order(styles, i) == second)
            second_z = i;
    }
    ok = ok && first_z < second_z;
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
    return ok;
}

static int fixture_works(void)
{
    char path[1024];
    char data[8192];
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    hddvd_xmu_style panel_style;
    FILE *file;
    size_t size, panel;
    int ok;
    snprintf(path, sizeof(path), "%s/plain_markup.xmu", HDDVD_FIXTURE_DIR);
    file = fopen(path, "rb");
    if (!file)
        return 0;
    size = fread(data, 1, sizeof(data), file);
    if (ferror(file) || !feof(file))
    {
        fclose(file);
        return 0;
    }
    fclose(file);
    ok = hddvd_xmu_open(data, size, &xmu) == HDDVD_OK &&
         hddvd_xmu_styles_open(xmu, &styles) == HDDVD_OK;
    panel = hddvd_xmu_find_id(xmu, "panel");
    ok = ok && panel != SIZE_MAX &&
         hddvd_xmu_get_computed_style(styles, panel, &panel_style) == HDDVD_OK &&
         panel_style.position_absolute && panel_style.x == 10 && panel_style.y == 20 &&
         panel_style.width == 200 && panel_style.height == 100;
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
    return ok;
}

static int rejected(const char *body)
{
    char xml[1024];
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    int rc;
    snprintf(xml, sizeof(xml), "<root xmlns=\"%s\" xmlns:s=\"%s\">%s</root>", NS, STYLE, body);
    rc = open_styles(xml, &xmu, &styles);
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
    return rc == HDDVD_EPARSE;
}

/* Constructs observed on real discs: display auto, percentage sizes, relative
   position, nav "none" and dangling nav targets, timing keyframes, and
   fragments that reference styles defined elsewhere. */
static int observed_constructs(void)
{
    char xml[2048];
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    hddvd_xmu_style style;
    size_t box, button, keyframe;
    int ok;
    snprintf(xml, sizeof(xml),
             "<root xmlns=\"%s\" xmlns:s=\"%s\"><head><styling>"
             "<style id=\"base\" s:width=\"10px\" s:navLeft=\"box\"/></styling>"
             "<timing><animate id=\"k\" s:x=\"1px;-2px\" s:display=\"auto\"/></timing></head>"
             "<body><div id=\"box\" style=\"base\" s:width=\"100%%\" s:height=\"5px\" "
             "s:display=\"auto\" s:contentWidth=\"100%%\"/>"
             "<button id=\"b\" style=\"base\" s:position=\"relative\" s:navLeft=\"none\" "
             "s:navRight=\"createdLater\"/></body></root>",
             NS, STYLE);
    ok = open_styles(xml, &xmu, &styles) == HDDVD_OK;
    box = hddvd_xmu_find_id(xmu, "box");
    button = hddvd_xmu_find_id(xmu, "b");
    keyframe = hddvd_xmu_find_id(xmu, "k");
    ok = ok && hddvd_xmu_get_computed_style(styles, box, &style) == HDDVD_OK &&
         (style.properties & HDDVD_XMU_STYLE_WIDTH_PERCENT) &&
         !(style.properties & HDDVD_XMU_STYLE_WIDTH) && style.width == 100 &&
         (style.properties & HDDVD_XMU_STYLE_HEIGHT) && style.height == 5 &&
         style.display == HDDVD_XMU_DISPLAY_AUTO;
    ok = ok && hddvd_xmu_get_computed_style(styles, button, &style) == HDDVD_OK &&
         (style.properties & HDDVD_XMU_STYLE_WIDTH) && style.width == 10 &&
         (style.properties & HDDVD_XMU_STYLE_POSITION) && !style.position_absolute &&
         (style.properties & HDDVD_XMU_STYLE_NAV_LEFT) && style.nav_left == SIZE_MAX &&
         style.nav_right == SIZE_MAX;
    ok = ok && hddvd_xmu_get_specified_style(styles, keyframe, &style) == HDDVD_OK &&
         style.properties == 0;
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
    styles = NULL;
    xmu = NULL;
    snprintf(xml, sizeof(xml),
             "<div xmlns=\"%s\" xmlns:s=\"%s\"><div id=\"d\" style=\"definedByHost\" "
             "s:x=\"3px\"/></div>",
             NS, STYLE);
    ok = ok && open_styles(xml, &xmu, &styles) == HDDVD_OK &&
         hddvd_xmu_get_computed_style(styles, hddvd_xmu_find_id(xmu, "d"), &style) == HDDVD_OK &&
         style.x == 3;
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
    return ok;
}

static void open_body(const char *body, hddvd_xmu **xmu, hddvd_xmu_styles **styles)
{
    char xml[4096];
    snprintf(xml, sizeof(xml), "<root xmlns=\"%s\" xmlns:s=\"%s\">%s</root>", NS, STYLE, body);
    assert(open_styles(xml, xmu, styles) == HDDVD_OK);
}

/* Detail properties as real discs declare them, with a style reference that
   the node's own declarations override. */
static void details_work(void)
{
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    hddvd_xmu_style_details d;
    size_t node, base, other;
    open_body("<head><styling><style id=\"base\" s:opacity=\"0.4\" s:color=\"#f7dc89\" "
              "s:font=\"Original.ttf\" s:fontSize=\"24px\" s:padding=\"5px\" s:navLeftUp=\"other\" "
              "s:navRightDown=\"none\" s:backgroundRepeat=\"no-repeat\"/></styling></head>"
              "<body><div id=\"n\" style=\"base\" s:opacity=\"inherit\" s:visibility=\"hidden\" "
              "s:backgroundColor=\"rgba(199,180,153,128)\" s:fontStyle=\"oblique\" "
              "s:textAlign=\"center\" s:displayAlign=\"after\" s:wrapOption=\"wrap\" "
              "s:textAltitude=\"140%\" s:anchor=\"startCenter\" s:paddingStart=\"36px\" "
              "s:border=\"1px solid white\" s:backgroundPositionHorizontal=\"-6px\" "
              "s:backgroundPositionVertical=\"30%\" s:backgroundFrame=\"2\" s:crop=\"0 0 20 33\" "
              "s:contentWidth=\"90%\" s:contentHeight=\"80%\" s:navIndex=\"none\"/>"
              "<div id=\"other\" s:color=\"blue\" s:backgroundColor=\"rgb(1,2,3)\" "
              "s:backgroundPositionVertical=\"bottom\" s:navIndex=\"7\" s:opacity=\"1.0\"/></body>",
              &xmu, &styles);
    node = hddvd_xmu_find_id(xmu, "n");
    base = hddvd_xmu_find_id(xmu, "base");
    other = hddvd_xmu_find_id(xmu, "other");

    assert(hddvd_xmu_get_style_details(styles, base, 0, &d) == HDDVD_OK);
    assert(d.opacity == 400 && !d.opacity_inherit && d.color == 0xf7dc89ffu &&
           !strcmp(d.font, "Original.ttf") && d.font_size == 24 && d.padding_before == 5 &&
           d.padding_after == 5 && d.padding_start == 5 && d.padding_end == 5);

    /* Specified: only the node's own declarations. */
    assert(hddvd_xmu_get_style_details(styles, node, 0, &d) == HDDVD_OK);
    assert(!(d.properties & HDDVD_XMU_DETAIL_FONT) && !d.font);
    assert((d.properties & HDDVD_XMU_DETAIL_OPACITY) && d.opacity_inherit);

    /* Computed: references first, then the node's own declarations. */
    assert(hddvd_xmu_get_style_details(styles, node, 1, &d) == HDDVD_OK);
    assert(d.opacity_inherit && d.visibility == HDDVD_XMU_VISIBILITY_HIDDEN);
    assert(d.color == 0xf7dc89ffu && d.background_color == 0xc7b49980u);
    assert(!strcmp(d.font, "Original.ttf") && d.font_size == 24 &&
           d.font_style == HDDVD_XMU_FONT_STYLE_OBLIQUE);
    assert(d.text_align == HDDVD_XMU_ALIGN_CENTER && d.display_align == HDDVD_XMU_ALIGN_END &&
           d.wrap == 1 && d.text_altitude_percent == 140 && !strcmp(d.anchor, "startCenter"));
    assert(d.padding_before == 5 && d.padding_start == 36 && !strcmp(d.border, "1px solid white"));
    assert(d.background_x == HDDVD_XMU_BACKGROUND_OFFSET && d.background_x_offset == -6 &&
           d.background_y == HDDVD_XMU_BACKGROUND_PERCENT && d.background_y_offset == 30);
    assert((d.properties & HDDVD_XMU_DETAIL_BACKGROUND_REPEAT) && d.background_repeat == 0);
    assert(d.background_frame == 2 && d.crop[0] == 0 && d.crop[2] == 20 && d.crop[3] == 33);
    assert(d.content_width_percent == 90 && d.content_height_percent == 80);
    assert(d.nav_left_up == other && d.nav_right_down == SIZE_MAX &&
           (d.properties & HDDVD_XMU_DETAIL_NAV_RIGHT_DOWN) && d.nav_left_down == SIZE_MAX &&
           !(d.properties & HDDVD_XMU_DETAIL_NAV_LEFT_DOWN));
    assert((d.properties & HDDVD_XMU_DETAIL_NAV_INDEX) && d.nav_index_none);

    assert(hddvd_xmu_get_style_details(styles, other, 1, &d) == HDDVD_OK);
    assert(d.color == 0x0000ffffu && d.background_color == 0x010203ffu &&
           d.background_y == HDDVD_XMU_BACKGROUND_END && d.nav_index == 7 && !d.nav_index_none &&
           d.opacity == 1000);
    assert(hddvd_xmu_styles_unmodeled_count(styles) == 0);
    assert(hddvd_xmu_get_style_details(styles, hddvd_xmu_node_count(xmu), 1, &d) == HDDVD_EINVAL);
    assert(hddvd_xmu_get_style_details(NULL, 0, 1, &d) == HDDVD_EINVAL);
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
}

/* Unknown properties and keywords are reported, deduplicated, and leave the
   property unset; keyframes inside <timing> are not reported. */
static void unmodeled_reported(void)
{
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_styles *styles = NULL;
    hddvd_xmu_style_details d;
    hddvd_xmu_style_unmodeled u;
    open_body("<head><timing><set s:futureProperty=\"1\"/></timing></head>"
              "<body><div id=\"a\" s:fontSize=\"small\" s:color=\"chartreuse\" "
              "s:futureProperty=\"x\"/><div s:fontSize=\"small\" s:futureProperty=\"y\" "
              "s:visibility=\"collapse\" s:opacity=\"auto\"/></body>",
              &xmu, &styles);
    assert(hddvd_xmu_styles_unmodeled_count(styles) == 5);
    assert(hddvd_xmu_styles_get_unmodeled(styles, 0, &u) == HDDVD_OK &&
           !strcmp(u.property, "fontSize") && !strcmp(u.value, "small") && u.occurrences == 2);
    assert(hddvd_xmu_styles_get_unmodeled(styles, 1, &u) == HDDVD_OK &&
           !strcmp(u.property, "color") && !strcmp(u.value, "chartreuse") && u.occurrences == 1);
    assert(hddvd_xmu_styles_get_unmodeled(styles, 2, &u) == HDDVD_OK &&
           !strcmp(u.property, "futureProperty") && !u.value && u.occurrences == 2);
    assert(hddvd_xmu_styles_get_unmodeled(styles, 3, &u) == HDDVD_OK &&
           !strcmp(u.property, "visibility"));
    assert(hddvd_xmu_styles_get_unmodeled(styles, 4, &u) == HDDVD_OK &&
           !strcmp(u.property, "opacity") && !strcmp(u.value, "auto"));
    assert(hddvd_xmu_styles_get_unmodeled(styles, 5, &u) == HDDVD_EINVAL);
    assert(hddvd_xmu_styles_get_unmodeled(styles, 0, NULL) == HDDVD_EINVAL);
    assert(hddvd_xmu_get_style_details(styles, hddvd_xmu_find_id(xmu, "a"), 1, &d) == HDDVD_OK &&
           !(d.properties & (HDDVD_XMU_DETAIL_FONT_SIZE | HDDVD_XMU_DETAIL_COLOR)));
    assert(hddvd_xmu_styles_unmodeled_count(NULL) == 0);
    hddvd_xmu_styles_close(styles);
    hddvd_xmu_close(xmu);
}

int main(void)
{
    const char *bad[] = {
        "<body><div s:x=\"1em\"/></body>", "<body><div s:width=\"-1px\"/></body>",
        "<body><div s:zIndex=\"2147483648\"/></body>", "<body><div s:display=\"inline\"/></body>",
        "<body><div s:position=\"fixed\"/></body>", "<body><div s:width=\"50.5%\"/></body>",
        "<body><div s:backgroundImage=\"url(no-quotes)\"/></body>",
        "<body><div style=\"missing\"/></body>",
        "<body><div id=\"notStyle\"/><div style=\"notStyle\"/></body>",
        "<head><styling><style id=\"a\" style=\"b\"/><style id=\"b\" style=\"a\"/>"
        "</styling></head>",
        /* Malformed numbers in detail properties. */
        "<body><div s:opacity=\"1.5\"/></body>", "<body><div s:opacity=\"0.\"/></body>",
        "<body><div s:fontSize=\"12em\"/></body>", "<body><div s:color=\"#12345\"/></body>",
        "<body><div s:color=\"rgba(1,2,3)\"/></body>",
        "<body><div s:backgroundColor=\"rgb(1,2,256)\"/></body>",
        "<body><div s:padding=\"-1px\"/></body>", "<body><div s:textAltitude=\"140\"/></body>",
        "<body><div s:backgroundPositionVertical=\"5pt\"/></body>",
        "<body><div s:backgroundFrame=\"-1\"/></body>", "<body><div s:crop=\"0 0 20\"/></body>",
        "<body><div s:navIndex=\"1x\"/></body>", "<body><div s:font=\"\"/></body>"};
    size_t i;
    if (!model_works() || !fixture_works() || !observed_constructs())
        return 1;
    details_work();
    unmodeled_reported();
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        if (!rejected(bad[i]))
            return 2;
    return 0;
}
