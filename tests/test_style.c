#include "hddvd.h"
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

int main(void)
{
    const char *bad[] = {"<body><div s:x=\"1em\"/></body>",
                         "<body><div s:width=\"-1px\"/></body>",
                         "<body><div s:zIndex=\"2147483648\"/></body>",
                         "<body><div s:display=\"inline\"/></body>",
                         "<body><div s:position=\"relative\"/></body>",
                         "<body><div s:backgroundImage=\"url(no-quotes)\"/></body>",
                         "<body><div style=\"missing\"/></body>",
                         "<body><div id=\"notStyle\"/><div style=\"notStyle\"/></body>",
                         "<head><styling><style id=\"a\" style=\"b\"/><style id=\"b\" style=\"a\"/>"
                         "</styling></head>",
                         "<body><button id=\"a\" s:navLeft=\"missing\"/></body>"};
    size_t i;
    if (!model_works() || !fixture_works())
        return 1;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        if (!rejected(bad[i]))
            return 2;
    return 0;
}
