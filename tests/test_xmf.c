#include "hddvd.h"
#include <stdio.h>
#include <stdlib.h>

static int read_file(const char *name, unsigned char **data, size_t *size)
{
    FILE *file = fopen(name, "rb");
    long length;

    if (!file || fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET)) {
        if (file)
            fclose(file);
        return 0;
    }
    *data = malloc((size_t)length);
    if (!*data || fread(*data, 1, (size_t)length, file) != (size_t)length) {
        free(*data);
        fclose(file);
        return 0;
    }
    fclose(file);
    *size = (size_t)length;
    return 1;
}

static int check(const char *leaf, int xmu)
{
    char path[1024];
    unsigned char *data = NULL;
    size_t size = 0;
    FILE *sink;
    int rc;

    snprintf(path, sizeof(path), "%s/%s", HDDVD_FIXTURE_DIR, leaf);
    if (!read_file(path, &data, &size))
        return 0;
    sink = tmpfile();
    if (!sink) {
        free(data);
        return 0;
    }
    rc = xmu ? hddvd_xmu_dump(data, size, leaf, sink)
             : hddvd_xmf_dump(data, size, leaf, sink);
    fclose(sink);
    free(data);
    return rc == HDDVD_OK;
}

int main(void)
{
    return check("plain_manifest.xmf", 0) && check("plain_markup.xmu", 1) ? 0 : 1;
}
