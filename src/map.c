#include "internal.h"
#include <stdio.h>
#include <string.h>

#define MAP_SECTOR 2048u
#define MAP_VTI_NAME_OFFSET 0x72u
#define MAP_HEADER_READ 0x100u

int hddvd_map_read_header(hddvd_stream *stream, hddvd_map_header *header)
{
    unsigned char b[MAP_HEADER_READ];
    const unsigned char *end;
    uint64_t size;
    size_t got = 0, i, length;
    int rc;
    if (!stream || !header)
        return HDDVD_EINVAL;
    memset(header, 0, sizeof(*header));
    size = hddvd_stream_size(stream);
    rc = hddvd_stream_read_at(stream, 0, b, sizeof(b), &got);
    if (rc)
        return rc;
    if (got < 12 || memcmp(b, "HDDVD_TMAP00", 12))
        return got < 12 ? HDDVD_EPARSE : HDDVD_ENOTSUP;
    if (got != sizeof(b) || size < MAP_SECTOR || size % MAP_SECTOR)
        return HDDVD_EPARSE;
    header->last_sector =
        (uint32_t)b[0x0c] << 24 | (uint32_t)b[0x0d] << 16 | (uint32_t)b[0x0e] << 8 | b[0x0f];
    if ((uint64_t)header->last_sector != size / MAP_SECTOR - 1)
        return HDDVD_EPARSE;
    end = memchr(b + MAP_VTI_NAME_OFFSET, 0, sizeof(b) - MAP_VTI_NAME_OFFSET);
    length = end ? (size_t)(end - (b + MAP_VTI_NAME_OFFSET)) : 0;
    if (!length || length >= sizeof(header->vti_name))
        return HDDVD_EPARSE;
    for (i = 0; i < length; i++)
        if (b[MAP_VTI_NAME_OFFSET + i] < 0x21 || b[MAP_VTI_NAME_OFFSET + i] > 0x7e)
            return HDDVD_EPARSE;
    memcpy(header->vti_name, b + MAP_VTI_NAME_OFFSET, length);
    header->size = size;
    return HDDVD_OK;
}

int hddvd_map_probe(const char *path, hddvd_map_info *o)
{
    FILE *f;
    long z;
    hddvd_stream *stream = NULL;
    hddvd_map_header header;
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
    o->structure_validated = hddvd_stream_open_file(path, &stream) == HDDVD_OK &&
                             hddvd_map_read_header(stream, &header) == HDDVD_OK;
    hddvd_stream_close(stream);
    return HDDVD_OK;
}
