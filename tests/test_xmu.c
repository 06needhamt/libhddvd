/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS "http://www.dvdforum.org/2005/ihd"
#define STATE_NS "http://www.dvdforum.org/2005/ihd#state"
#define STYLE_NS "http://www.dvdforum.org/2005/ihd#style"

static int parse(const char *xml, int expected)
{
    hddvd_xmu *xmu = NULL;
    int rc = hddvd_xmu_open(xml, strlen(xml), &xmu);
    hddvd_xmu_close(xmu);
    return rc == expected;
}

static int model_works(void)
{
    const char xml[] =
        "<root xmlns=\"" NS "\" xmlns:state=\"" STATE_NS "\" xmlns:style=\"" STYLE_NS "\">"
        "<head><styling><style id=\"future\" style:x=\"2px\"/></styling>"
        "<timing clock=\"title\"/></head><body><div id=\"container\">"
        "<button id=\"first\" style:navRight=\"future\" state:focused=\"true\"/>"
        "<futureElement custom=\"kept\">text</futureElement></div></body></root>";
    hddvd_xmu *xmu = NULL;
    hddvd_xmu_node node;
    hddvd_xmu_attribute attribute;
    size_t first, future;
    int ok = hddvd_xmu_open(xml, sizeof(xml) - 1, &xmu) == HDDVD_OK;
    first = hddvd_xmu_find_id(xmu, "first");
    future = hddvd_xmu_find_id(xmu, "future");
    ok = ok && hddvd_xmu_node_count(xmu) == 9 && first != SIZE_MAX && future != SIZE_MAX &&
         hddvd_xmu_initial_focus(xmu) == first && !strcmp(hddvd_xmu_clock(xmu), "title") &&
         hddvd_xmu_find_id(xmu, "missing") == SIZE_MAX;
    ok = ok && hddvd_xmu_get_node(xmu, first, &node) == HDDVD_OK && !strcmp(node.name, "button") &&
         !strcmp(node.id, "first") && node.attribute_count == 3 && node.next_sibling != SIZE_MAX;
    ok = ok && hddvd_xmu_get_attribute(xmu, first, 1, &attribute) == HDDVD_OK &&
         !strcmp(attribute.namespace_uri, STYLE_NS) && !strcmp(attribute.name, "navRight") &&
         !strcmp(attribute.value, "future");
    ok = ok && hddvd_xmu_get_attribute(xmu, first, 3, &attribute) == HDDVD_EINVAL;
    hddvd_xmu_close(xmu);
    return ok;
}

/* Focus set by timing templates under <head> is not the initial focus, and
   a <div>-rooted fragment parses. */
static int focus_and_fragments(void)
{
    const char document[] =
        "<root xmlns=\"" NS "\" xmlns:state=\"" STATE_NS "\"><head><timing><defs>"
        "<g id=\"hover\"><set state:focused=\"true\"/></g>"
        "<g id=\"focus\"><set state:focused=\"true\"/></g></defs></timing></head>"
        "<body><button id=\"b\" state:focused=\"true\"/></body></root>";
    const char template_only[] =
        "<root xmlns=\"" NS "\" xmlns:state=\"" STATE_NS "\"><head><timing>"
        "<set state:focused=\"true\"/></timing></head><body/></root>";
    const char fragment[] = "<div xmlns=\"" NS "\" xmlns:state=\"" STATE_NS "\"><div>"
                            "<button id=\"f\" state:focused=\"true\"/></div></div>";
    hddvd_xmu *xmu = NULL;
    int ok = hddvd_xmu_open(document, sizeof(document) - 1, &xmu) == HDDVD_OK &&
             hddvd_xmu_initial_focus(xmu) == hddvd_xmu_find_id(xmu, "b");
    hddvd_xmu_close(xmu);
    xmu = NULL;
    ok = ok && hddvd_xmu_open(template_only, sizeof(template_only) - 1, &xmu) == HDDVD_OK &&
         hddvd_xmu_initial_focus(xmu) == SIZE_MAX;
    hddvd_xmu_close(xmu);
    xmu = NULL;
    ok = ok && hddvd_xmu_open(fragment, sizeof(fragment) - 1, &xmu) == HDDVD_OK &&
         hddvd_xmu_initial_focus(xmu) == hddvd_xmu_find_id(xmu, "f");
    hddvd_xmu_close(xmu);
    return ok;
}

static int limits_work(void)
{
    const char xml[] = "<root xmlns=\"" NS "\" a=\"1\"><body><div/></body></root>";
    hddvd_xmu_limits limits = {2, 8, 8};
    hddvd_xmu *xmu = NULL;
    int ok = hddvd_xmu_open_with_limits(xml, sizeof(xml) - 1, &limits, &xmu) == HDDVD_ELIMIT;
    limits.max_depth = 4;
    limits.max_nodes = 2;
    ok = ok && hddvd_xmu_open_with_limits(xml, sizeof(xml) - 1, &limits, &xmu) == HDDVD_ELIMIT;
    limits.max_nodes = 8;
    limits.max_attributes = 1;
    ok = ok && hddvd_xmu_open_with_limits(xml, sizeof(xml) - 1, &limits, &xmu) == HDDVD_OK;
    hddvd_xmu_close(xmu);
    limits.max_attributes = 0;
    ok = ok && hddvd_xmu_open_with_limits(xml, sizeof(xml) - 1, &limits, &xmu) == HDDVD_EINVAL;
    return ok;
}

int main(void)
{
    const char *bad[] = {
        "<root xmlns=\"wrong\"/>",
        "<notroot xmlns=\"" NS "\"/>",
        "<root xmlns=\"" NS "\"><body><button id=\"same\"/><button id=\"same\"/></body></root>",
        "<root xmlns=\"" NS "\" xmlns:s=\"" STATE_NS
        "\"><body><button s:focused=\"true\"/><button s:focused=\"true\"/></body></root>",
        "<div xmlns=\"wrong\"/>",
        "<span xmlns=\"" NS "\"/>"};
    size_t i;
    if (!model_works() || !focus_and_fragments() || !limits_work())
        return 1;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        if (!parse(bad[i], HDDVD_EPARSE))
            return 2;
    return 0;
}
