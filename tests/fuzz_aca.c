#include "hddvd.h"
#include <stdint.h>

static int visit(void *opaque, const hddvd_aca_entry *entry)
{
    (void)opaque;
    (void)entry;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_stream *stream;
    hddvd_aca *archive;
    if (hddvd_stream_open_memory(data, size, &stream) != HDDVD_OK)
        return 0;
    if (hddvd_aca_open_stream(stream, &archive) == HDDVD_OK) {
        size_t i;
        for (i = 0; i < hddvd_aca_count(archive); i++) {
            hddvd_aca_entry entry;
            if (hddvd_aca_get_entry(archive, i, &entry) == HDDVD_OK)
                (void)visit(NULL, &entry);
        }
        hddvd_aca_close(archive);
    }
    hddvd_stream_close(stream);
    return 0;
}
