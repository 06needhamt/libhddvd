#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Layout notes and evidence: docs/formats/ifo-map.md. */
#define VTI_SECTOR 2048u
#define VTI_EVOB_TABLE_POINTER 0xbcu
#define VTI_TABLE_HEADER 8u
#define VTI_EVOB_RECORD 0x140u
#define VTI_NAME_OFFSET 0x02u
#define VTI_NAME_SIZE 256u
#define VTI_NUMBER_OFFSET 0x108u
#define VTI_START_OFFSET 0x10au
#define VTI_END_OFFSET 0x10eu
#define VTI_SECTORS_OFFSET 0x112u
#define VTI_ATTRIBUTE_OFFSET 0x116u

struct hddvd_vti
{
    hddvd_vti_evob *evobs;
    size_t count;
};

static uint16_t be16(const unsigned char *b)
{
    return (uint16_t)(b[0] << 8 | b[1]);
}

static uint32_t be32(const unsigned char *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

static int read_exact(hddvd_stream *stream, uint64_t offset, void *out, size_t size)
{
    size_t got = 0;
    int rc = hddvd_stream_read_at(stream, offset, out, size, &got);
    if (rc)
        return rc;
    return got == size ? HDDVD_OK : HDDVD_EPARSE;
}

static int parse_record(const unsigned char *r, hddvd_vti_evob *evob)
{
    const unsigned char *end = memchr(r + VTI_NAME_OFFSET, 0, VTI_NAME_SIZE);
    size_t length, i;
    if (!end || end == r + VTI_NAME_OFFSET)
        return HDDVD_EPARSE;
    length = (size_t)(end - (r + VTI_NAME_OFFSET));
    for (i = 0; i < length; i++)
    {
        unsigned char c = r[VTI_NAME_OFFSET + i];
        if (c < 0x21 || c > 0x7e || c == '/' || c == '\\')
            return HDDVD_EPARSE;
    }
    memset(evob, 0, sizeof(*evob));
    memcpy(evob->file_name, r + VTI_NAME_OFFSET, length);
    evob->number = be16(r + VTI_NUMBER_OFFSET);
    evob->start.ticks = be32(r + VTI_START_OFFSET);
    evob->end.ticks = be32(r + VTI_END_OFFSET);
    evob->sector_count = be32(r + VTI_SECTORS_OFFSET);
    evob->attribute_number = be16(r + VTI_ATTRIBUTE_OFFSET);
    if (evob->end.ticks < evob->start.ticks || !evob->sector_count)
        return HDDVD_EPARSE;
    return HDDVD_OK;
}

int hddvd_vti_open(hddvd_stream *stream, hddvd_vti **out)
{
    hddvd_ifo_header header;
    unsigned char pointer[4], table[VTI_TABLE_HEADER], offset[4], record[VTI_EVOB_RECORD];
    uint64_t base, table_size;
    hddvd_vti *vti;
    size_t i, j, count;
    int rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    rc = hddvd_ifo_read_header(stream, &header);
    if (rc)
        return rc;
    if (header.kind != HDDVD_IFO_ADVANCED_VTS)
        return HDDVD_ENOTSUP;
    rc = read_exact(stream, VTI_EVOB_TABLE_POINTER, pointer, sizeof(pointer));
    if (rc)
        return rc;
    /* The table pointer is a sector number; sector 0 holds this header. */
    if (!be32(pointer) || be32(pointer) > header.last_sector)
        return HDDVD_EPARSE;
    base = (uint64_t)be32(pointer) * VTI_SECTOR;
    rc = read_exact(stream, base, table, sizeof(table));
    if (rc)
        return rc;
    count = be16(table + 2);
    /* The end address is the offset of the table's last byte. */
    table_size = (uint64_t)be32(table + 4) + 1;
    if (base + table_size > header.size || table_size < VTI_TABLE_HEADER + 4u * (uint64_t)count)
        return HDDVD_EPARSE;
    vti = calloc(1, sizeof(*vti));
    if (!vti)
        return HDDVD_ENOMEM;
    if (count && !(vti->evobs = calloc(count, sizeof(*vti->evobs))))
    {
        free(vti);
        return HDDVD_ENOMEM;
    }
    for (i = 0; i < count; i++)
    {
        uint32_t at;
        rc = read_exact(stream, base + VTI_TABLE_HEADER + 4u * i, offset, sizeof(offset));
        if (rc)
            goto fail;
        at = be32(offset);
        rc = HDDVD_EPARSE;
        if (at < VTI_TABLE_HEADER + 4u * count || (uint64_t)at + VTI_EVOB_RECORD > table_size)
            goto fail;
        rc = read_exact(stream, base + at, record, sizeof(record));
        if (!rc)
            rc = parse_record(record, &vti->evobs[i]);
        if (rc)
            goto fail;
        for (j = 0; j < i; j++)
            if (vti->evobs[j].number == vti->evobs[i].number)
            {
                rc = HDDVD_EPARSE;
                goto fail;
            }
        vti->count++;
    }
    *out = vti;
    return HDDVD_OK;
fail:
    hddvd_vti_close(vti);
    return rc;
}

void hddvd_vti_close(hddvd_vti *vti)
{
    if (!vti)
        return;
    free(vti->evobs);
    free(vti);
}

size_t hddvd_vti_evob_count(const hddvd_vti *vti)
{
    return vti ? vti->count : 0;
}

int hddvd_vti_get_evob(const hddvd_vti *vti, size_t index, hddvd_vti_evob *out)
{
    if (!vti || !out || index >= vti->count)
        return HDDVD_EINVAL;
    *out = vti->evobs[index];
    return HDDVD_OK;
}

int hddvd_vti_find_evob(const hddvd_vti *vti, uint16_t number, size_t *index)
{
    size_t i;
    if (!vti || !index)
        return HDDVD_EINVAL;
    for (i = 0; i < vti->count; i++)
        if (vti->evobs[i].number == number)
        {
            *index = i;
            return HDDVD_OK;
        }
    return HDDVD_ENOTFOUND;
}
