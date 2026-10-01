#include "internal.h"
#include <stdio.h>
int hddvd_map_probe(const char *path, hddvd_map_info *o)
{
    FILE *f;
    long z;
    if (!path || !o)
        return HDDVD_EINVAL;
    f = fopen(path, "rb");
    if (!f)
        return HDDVD_EIO;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return HDDVD_EIO;
    }
    z = ftell(f);
    fclose(f);
    if (z < 0)
        return HDDVD_EIO;
    o->size = (uint64_t)z;
    o->sector_count = (uint32_t)(((uint64_t)z + 2047u) / 2048u);
    o->structure_validated = 0;
    return HDDVD_OK;
}
