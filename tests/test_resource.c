#include "hddvd.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *missing = "file:///dvddisc/definitely-missing-resource.bin";
    const char *present = "file:///dvddisc/diagnostic-resource.bin";
    hddvd_error_detail detail;
    hddvd_stream *stream = NULL;
    hddvd_ctx *context;
    FILE *file;

    context = hddvd_open(".");
    assert(context != NULL);
    assert(hddvd_get_last_error(context, &detail) == HDDVD_OK);
    assert(detail.status == HDDVD_OK);

    assert(hddvd_open_resource(context, "not-a-uri", &stream) == HDDVD_EPARSE);
    assert(hddvd_get_last_error(context, &detail) == HDDVD_OK);
    assert(detail.status == HDDVD_EPARSE);
    assert(detail.subsystem == HDDVD_SUBSYSTEM_URI);
    assert(strcmp(detail.resource, "not-a-uri") == 0);
    assert(strstr(detail.message, "URI") != NULL);

    assert(hddvd_open_resource(context, missing, &stream) == HDDVD_ENOTFOUND);
    assert(hddvd_get_last_error(context, &detail) == HDDVD_OK);
    assert(detail.status == HDDVD_ENOTFOUND);
    assert(detail.subsystem == HDDVD_SUBSYSTEM_RESOURCE);
    assert(strcmp(detail.resource, missing) == 0);

    file = fopen("diagnostic-resource.bin", "wb");
    assert(file != NULL);
    assert(fwrite("ok", 1, 2, file) == 2);
    assert(fclose(file) == 0);
    assert(hddvd_open_resource(context, present, &stream) == HDDVD_OK);
    assert(stream != NULL);
    hddvd_stream_close(stream);
    assert(remove("diagnostic-resource.bin") == 0);
    assert(hddvd_get_last_error(context, &detail) == HDDVD_OK);
    assert(detail.status == HDDVD_OK);
    assert(detail.subsystem == HDDVD_SUBSYSTEM_NONE);

    hddvd_close(context);
    assert(hddvd_get_last_error(NULL, &detail) == HDDVD_EINVAL);
    return 0;
}
