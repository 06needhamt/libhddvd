#include "hddvd.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int visit(void *opaque, const hddvd_aca_entry *entry)
{
    (void)opaque;
    (void)entry;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char path[128];
    FILE *file;
    size_t bad;

    snprintf(path, sizeof(path), "/tmp/libhddvd-fuzz-%ld.aca", (long)getpid());
    file = fopen(path, "wb");
    if (!file)
        return 0;
    if (size && fwrite(data, 1, size, file) != size) {
        fclose(file);
        remove(path);
        return 0;
    }
    fclose(file);
    (void)hddvd_aca_list(path, visit, NULL);
    (void)hddvd_aca_verify(path, &bad);
    remove(path);
    return 0;
}
