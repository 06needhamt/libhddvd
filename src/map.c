#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
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

/* Entry table pointer, EVOB number, and entry count; see docs/formats/ifo-map.md. */
#define TMAP_TABLE_POINTER 0x180u
#define TMAP_TABLE_MIN (TMAP_TABLE_POINTER + 8u)

struct hddvd_tmap
{
    hddvd_map_header header;
    uint16_t evob_number;
    hddvd_tmap_entry *entries;
    size_t count;
    uint64_t total_units;
    uint64_t total_sectors;
};

/* Stored playback time is in units of 3003/8 ticks (a tenth of a
   24000/1001 frame); cumulative values are converted so that rounding never
   accumulates. */
static uint64_t tmap_ticks(uint64_t units)
{
    return units * 3003u / 8u;
}

int hddvd_tmap_open(hddvd_stream *stream, hddvd_tmap **out)
{
    unsigned char g[8], e[4];
    hddvd_tmap *tmap;
    uint32_t table;
    uint64_t units = 0, sectors = 0;
    size_t got = 0, i, count;
    int rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!stream)
        return HDDVD_EINVAL;
    tmap = calloc(1, sizeof(*tmap));
    if (!tmap)
        return HDDVD_ENOMEM;
    rc = hddvd_map_read_header(stream, &tmap->header);
    if (!rc)
        rc = hddvd_stream_read_at(stream, TMAP_TABLE_POINTER, g, sizeof(g), &got);
    if (!rc && got != sizeof(g))
        rc = HDDVD_EPARSE;
    if (rc)
        goto fail;
    table = (uint32_t)g[0] << 24 | (uint32_t)g[1] << 16 | (uint32_t)g[2] << 8 | g[3];
    tmap->evob_number = (uint16_t)(g[4] << 8 | g[5]);
    count = (size_t)(g[6] << 8 | g[7]);
    rc = HDDVD_EPARSE;
    if (!count || table < TMAP_TABLE_MIN || table % 4u ||
        (uint64_t)table + 4u * (uint64_t)count > tmap->header.size)
        goto fail;
    tmap->entries = calloc(count, sizeof(*tmap->entries));
    if (!tmap->entries)
    {
        rc = HDDVD_ENOMEM;
        goto fail;
    }
    for (i = 0; i < count; i++)
    {
        hddvd_tmap_entry *entry = &tmap->entries[i];
        uint32_t v;
        rc = hddvd_stream_read_at(stream, (uint64_t)table + 4u * i, e, sizeof(e), &got);
        if (!rc && got != sizeof(e))
            rc = HDDVD_EPARSE;
        if (rc)
            goto fail;
        v = (uint32_t)e[0] << 24 | (uint32_t)e[1] << 16 | (uint32_t)e[2] << 8 | e[3];
        entry->first_reference_sectors = v >> 21;
        entry->duration_units = v >> 11 & 0x3ffu;
        entry->sector_count = v & 0x7ffu;
        if (!entry->sector_count || !entry->duration_units || !entry->first_reference_sectors ||
            entry->first_reference_sectors > entry->sector_count)
        {
            rc = HDDVD_EPARSE;
            goto fail;
        }
        /* At most 65535 entries of 2047 sectors, so this fits 32 bits. */
        entry->first_sector = (uint32_t)sectors;
        entry->start.ticks = tmap_ticks(units);
        units += entry->duration_units;
        sectors += entry->sector_count;
        entry->duration.ticks = tmap_ticks(units) - entry->start.ticks;
    }
    tmap->count = count;
    tmap->total_units = units;
    tmap->total_sectors = sectors;
    *out = tmap;
    return HDDVD_OK;
fail:
    hddvd_tmap_close(tmap);
    return rc;
}

void hddvd_tmap_close(hddvd_tmap *tmap)
{
    if (!tmap)
        return;
    free(tmap->entries);
    free(tmap);
}

int hddvd_tmap_get_header(const hddvd_tmap *tmap, hddvd_map_header *header)
{
    if (!tmap || !header)
        return HDDVD_EINVAL;
    *header = tmap->header;
    return HDDVD_OK;
}

uint16_t hddvd_tmap_evob_number(const hddvd_tmap *tmap)
{
    return tmap ? tmap->evob_number : 0;
}

size_t hddvd_tmap_entry_count(const hddvd_tmap *tmap)
{
    return tmap ? tmap->count : 0;
}

int hddvd_tmap_get_entry(const hddvd_tmap *tmap, size_t index, hddvd_tmap_entry *entry)
{
    if (!tmap || !entry || index >= tmap->count)
        return HDDVD_EINVAL;
    *entry = tmap->entries[index];
    return HDDVD_OK;
}

uint64_t hddvd_tmap_sector_count(const hddvd_tmap *tmap)
{
    return tmap ? tmap->total_sectors : 0;
}

hddvd_time hddvd_tmap_duration(const hddvd_tmap *tmap)
{
    hddvd_time t;
    t.ticks = tmap ? tmap_ticks(tmap->total_units) : 0;
    return t;
}

int hddvd_tmap_find(const hddvd_tmap *tmap, hddvd_time offset, size_t *index)
{
    size_t lo = 0, hi;
    if (!tmap || !index)
        return HDDVD_EINVAL;
    if (offset.ticks >= tmap_ticks(tmap->total_units))
        return HDDVD_ENOTFOUND;
    /* Find the last entry starting at or before the offset. */
    hi = tmap->count;
    while (hi - lo > 1)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (tmap->entries[mid].start.ticks <= offset.ticks)
            lo = mid;
        else
            hi = mid;
    }
    *index = lo;
    return HDDVD_OK;
}

int hddvd_tmap_check_evob(const hddvd_tmap *tmap, const hddvd_vti_evob *evob)
{
    if (!tmap || !evob)
        return HDDVD_EINVAL;
    if (tmap->evob_number != evob->number || tmap->total_sectors != evob->sector_count ||
        evob->end.ticks < evob->start.ticks ||
        tmap_ticks(tmap->total_units) != evob->end.ticks - evob->start.ticks)
        return HDDVD_EPARSE;
    return HDDVD_OK;
}
