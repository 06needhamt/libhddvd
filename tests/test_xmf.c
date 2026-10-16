#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS "http://www.dvdforum.org/2005/HDDVDVideo/Manifest"

static int expect_parse(const char *xml, int expected)
{
    hddvd_xmf *xmf = NULL;
    int rc = hddvd_xmf_open(xml, strlen(xml), &xmf);
    hddvd_xmf_close(xmf);
    return rc == expected;
}

static int valid_model(void)
{
    const char xml[] =
        "<Application xmlns=\"" NS "\" id=\"app\">"
        "<Region x=\"0\" y=\"1\" width=\"1920\" height=\"1080\"/>"
        "<Script id=\"s1\" src=\"one.js\"/><Script id=\"s2\" src=\"two.js\"/>"
        "<Markup id=\"m1\" src=\"main.xmu\"/><Resource src=\"file:///dvddisc/ADV_OBJ\"/>"
        "<Future optional=\"yes\"/></Application>";
    hddvd_xmf *xmf = NULL;
    hddvd_xmf_region region;
    const char *id, *src;
    int ok = hddvd_xmf_open(xml, sizeof(xml) - 1, &xmf) == HDDVD_OK;
    ok = ok && !strcmp(hddvd_xmf_application_id(xmf), "app") &&
         hddvd_xmf_get_region(xmf, &region) == HDDVD_OK && region.x == 0 && region.y == 1 &&
         region.width == 1920 && region.height == 1080 && hddvd_xmf_script_count(xmf) == 2 &&
         hddvd_xmf_markup_count(xmf) == 1 && hddvd_xmf_resource_root_count(xmf) == 1;
    ok = ok && hddvd_xmf_get_script(xmf, 1, &id, &src) == HDDVD_OK && !strcmp(id, "s2") &&
         !strcmp(src, "two.js") && hddvd_xmf_get_markup(xmf, 0, &id, &src) == HDDVD_OK &&
         !strcmp(id, "m1") && !strcmp(src, "main.xmu") &&
         !strcmp(hddvd_xmf_resource_root(xmf, 0), "file:///dvddisc/ADV_OBJ") &&
         hddvd_xmf_get_script(xmf, 2, &id, &src) == HDDVD_EINVAL;
    hddvd_xmf_close(xmf);
    return ok;
}

/* Observed manifests usually omit the application id and give script and
   markup references only a src. */
static int optional_ids(void)
{
    const char xml[] = "<Application xmlns=\"" NS "\"><Region x=\"0\" y=\"0\" width=\"1\" "
                       "height=\"1\"/><Script src=\"a.js\"/><Script src=\"b.js\"/>"
                       "<Markup src=\"m.xmu\"/><Script id=\"s\" src=\"c.js\"/></Application>";
    hddvd_xmf *xmf = NULL;
    const char *id = "unset", *src = NULL;
    int ok = hddvd_xmf_open(xml, sizeof(xml) - 1, &xmf) == HDDVD_OK;
    ok = ok && hddvd_xmf_application_id(xmf) == NULL && hddvd_xmf_script_count(xmf) == 3 &&
         hddvd_xmf_get_script(xmf, 1, &id, &src) == HDDVD_OK && id == NULL &&
         !strcmp(src, "b.js") && hddvd_xmf_get_script(xmf, 2, &id, &src) == HDDVD_OK &&
         !strcmp(id, "s");
    hddvd_xmf_close(xmf);
    return ok;
}

static int limits_work(void)
{
    const char xml[] = "<Application xmlns=\"" NS "\" id=\"a\"><Region x=\"0\" y=\"0\" width=\"1\" "
                       "height=\"1\"/><Script id=\"s\" src=\"s\"/></Application>";
    hddvd_xmf_limits limits = {2, 5};
    hddvd_xmf *xmf = NULL;
    int ok = hddvd_xmf_open_with_limits(xml, sizeof(xml) - 1, &limits, &xmf) == HDDVD_ELIMIT;
    limits.max_nodes = 5;
    limits.max_references = 1;
    ok = ok && hddvd_xmf_open_with_limits(xml, sizeof(xml) - 1, &limits, &xmf) == HDDVD_OK;
    hddvd_xmf_close(xmf);
    return ok;
}

int main(void)
{
    const char *bad[] = {
        "<Application xmlns=\"wrong\" id=\"a\"><Region x=\"0\" y=\"0\" width=\"1\" "
        "height=\"1\"/></Application>",
        "<Application xmlns=\"" NS
        "\" id=\"\"><Region x=\"0\" y=\"0\" width=\"1\" height=\"1\"/></Application>",
        "<Application xmlns=\"" NS "\"><Region x=\"0\" y=\"0\" width=\"1\" height=\"1\"/>"
        "<Script id=\"s\" src=\"a\"/><Markup id=\"s\" src=\"b\"/></Application>",
        "<Application xmlns=\"" NS "\"><Region x=\"0\" y=\"0\" width=\"1\" height=\"1\"/>"
        "<Script src=\"\"/></Application>",
        "<Application xmlns=\"" NS "\" id=\"a\"></Application>",
        "<Application xmlns=\"" NS
        "\" id=\"a\"><Region x=\"-1\" y=\"0\" width=\"1\" height=\"1\"/></Application>",
        "<Application xmlns=\"" NS
        "\" id=\"a\"><Region x=\"0\" y=\"0\" width=\"0\" height=\"1\"/></Application>",
        "<Application xmlns=\"" NS "\" id=\"a\"><Region x=\"0\" y=\"0\" width=\"1\" "
        "height=\"1\"/><Script id=\"a\" src=\"x\"/></Application>",
        "<Application xmlns=\"" NS
        "\" id=\"a\"><Region x=\"0\" y=\"0\" width=\"1\" height=\"1\"/><Script xmlns=\"wrong\" "
        "id=\"s\" src=\"x\"/></Application>"};
    size_t i;
    if (!valid_model() || !optional_ids() || !limits_work())
        return 1;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        if (!expect_parse(bad[i], HDDVD_EPARSE))
            return 2;
    return 0;
}
