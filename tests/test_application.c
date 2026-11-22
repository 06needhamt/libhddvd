/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void write_file(const char *name, const char *contents)
{
    FILE *file = fopen(name, "wb");
    assert(file);
    assert(fwrite(contents, 1, strlen(contents), file) == strlen(contents));
    assert(fclose(file) == 0);
}

int main(void)
{
    static const char manifest[] =
        "<Application xmlns='http://www.dvdforum.org/2005/HDDVDVideo/Manifest' id='a'>"
        "<Region x='0' y='0' width='1920' height='1080'/>"
        "<Script id='s' src='file:///dvddisc/missing.js'/>"
        "<Script id='bad' src='file:///dvddisc/bad.js'/>"
        "<Markup id='m' src='file:///dvddisc/app.xmu'/>"
        "<Resource src='file:///dvddisc/no-such-root'/>"
        "<Resource src='file:///dvddisc'/></Application>";
    static const char markup[] =
        "<root xmlns='http://www.dvdforum.org/2005/ihd' "
        "xmlns:style='http://www.dvdforum.org/2005/ihd#style'><body>"
        "<button id='b' style:backgroundImage=\"url('image.png') url('missing.png')\" "
        "style:futureProperty='value'/><future/>"
        "</body></root>";
    hddvd_application *application = NULL;
    hddvd_dependency dependency;
    hddvd_requirement requirement;
    hddvd_ctx *context;
    size_t i;
    int saw_future = 0, saw_future_property = 0, saw_missing_script = 0, saw_bad_script = 0;
    int saw_image = 0, saw_missing_image = 0;
    write_file("app.xmf", manifest);
    write_file("app.xmu", markup);
    write_file("bad.js", "not an HDi script");
    write_file("image.png", "synthetic image bytes");
    context = hddvd_open(".");
    assert(context);
    assert(hddvd_application_open(context, "file:///dvddisc/app.xmf", &application) == HDDVD_OK);
    assert(hddvd_application_manifest(application));
    assert(hddvd_application_dependency_count(application) == 5);
    for (i = 0; i < hddvd_application_dependency_count(application); i++)
    {
        assert(hddvd_application_get_dependency(application, i, &dependency) == HDDVD_OK);
        if (dependency.kind == HDDVD_DEPENDENCY_SCRIPT && !strcmp(dependency.id, "s"))
            saw_missing_script = dependency.status == HDDVD_ENOTFOUND;
        if (dependency.kind == HDDVD_DEPENDENCY_SCRIPT && !strcmp(dependency.id, "bad"))
            saw_bad_script = dependency.status == HDDVD_EPARSE;
        if (dependency.kind == HDDVD_DEPENDENCY_IMAGE)
        {
            if (strstr(dependency.uri, "image.png"))
                saw_image = dependency.status == HDDVD_OK &&
                            !strcmp(dependency.uri, "file:///dvddisc/image.png");
            if (strstr(dependency.uri, "missing.png"))
                saw_missing_image = dependency.status == HDDVD_ENOTFOUND;
        }
    }
    for (i = 0; i < hddvd_application_requirement_count(application); i++)
    {
        assert(hddvd_application_get_requirement(application, i, &requirement) == HDDVD_OK);
        if (!strcmp(requirement.name, "future"))
            saw_future = !requirement.supported && requirement.occurrences == 1 &&
                         requirement.namespace_uri != NULL;
        if (!strcmp(requirement.name, "futureProperty"))
            saw_future_property =
                !requirement.supported && requirement.kind == HDDVD_REQUIREMENT_STYLE_PROPERTY;
    }
    assert(saw_missing_script && saw_bad_script && saw_image && saw_missing_image && saw_future &&
           saw_future_property);
    assert(hddvd_application_get_dependency(application, 5, &dependency) == HDDVD_EINVAL);
    hddvd_application_close(application);
    hddvd_close(context);
    assert(remove("app.xmf") == 0);
    assert(remove("app.xmu") == 0);
    assert(remove("bad.js") == 0);
    assert(remove("image.png") == 0);
    return 0;
}
