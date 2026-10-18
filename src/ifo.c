#include "internal.h"
#include <stdio.h>
#include <string.h>

#define IFO_SECTOR 2048u
#define IFO_LAST_SECTOR_OFFSET 0x1cu

static const struct
{
    const char *identifier;
    hddvd_ifo_kind kind;
} ifo_identifiers[] = {{"HVDVD-VMG100", HDDVD_IFO_VMG},
                       {"STANDARD-VTS", HDDVD_IFO_STANDARD_VTS},
                       {"ADVANCED-VTS", HDDVD_IFO_ADVANCED_VTS}};

static hddvd_ifo_kind ifo_kind(const unsigned char *identifier)
{
    size_t i;
    for (i = 0; i < sizeof(ifo_identifiers) / sizeof(ifo_identifiers[0]); i++)
        if (!memcmp(identifier, ifo_identifiers[i].identifier, 12))
            return ifo_identifiers[i].kind;
    return HDDVD_IFO_UNKNOWN;
}

int hddvd_ifo_probe(const char *path, hddvd_ifo_info *o)
{
    FILE *f;
    unsigned char b[16] = {0};
    long z;
    if (!path || !o)
        return HDDVD_EINVAL;
    memset(o, 0, sizeof(*o));
    f = fopen(path, "rb");
    if (!f)
        return HDDVD_EIO;
    if (fread(b, 1, 16, f) < 12)
    {
        fclose(f);
        return HDDVD_EPARSE;
    }
    if (fseek(f, 0, SEEK_END) || (z = ftell(f)) < 0)
    {
        fclose(f);
        return HDDVD_EIO;
    }
    fclose(f);
    o->size = (uint64_t)z;
    memcpy(o->identifier, b, 16);
    o->identifier[16] = 0;
    o->recognized_identifier =
        ifo_kind(b) != HDDVD_IFO_UNKNOWN || !memcmp(b, "DVDVIDEO-", 9) || !memcmp(b, "HDDVD", 5);
    return HDDVD_OK;
}

int hddvd_ifo_read_header(hddvd_stream *stream, hddvd_ifo_header *header)
{
    unsigned char b[IFO_LAST_SECTOR_OFFSET + 4];
    uint64_t size;
    size_t got = 0;
    int rc;
    if (!stream || !header)
        return HDDVD_EINVAL;
    memset(header, 0, sizeof(*header));
    size = hddvd_stream_size(stream);
    rc = hddvd_stream_read_at(stream, 0, b, sizeof(b), &got);
    if (rc)
        return rc;
    if (got != sizeof(b))
        return HDDVD_EPARSE;
    header->kind = ifo_kind(b);
    if (header->kind == HDDVD_IFO_UNKNOWN)
        return HDDVD_ENOTSUP;
    if (size < IFO_SECTOR || size % IFO_SECTOR)
        return HDDVD_EPARSE;
    header->last_sector =
        (uint32_t)b[0x1c] << 24 | (uint32_t)b[0x1d] << 16 | (uint32_t)b[0x1e] << 8 | b[0x1f];
    if ((uint64_t)header->last_sector != size / IFO_SECTOR - 1)
        return HDDVD_EPARSE;
    memcpy(header->identifier, b, 12);
    header->identifier[12] = 0;
    header->size = size;
    return HDDVD_OK;
}

void hddvd_decode_video_attributes(const unsigned char *raw, hddvd_video_attributes *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->raw, raw, sizeof(out->raw));
    out->coding = raw[0] >> 6;
}

void hddvd_decode_stream_attributes(const unsigned char *raw, size_t size, size_t language_at,
                                    hddvd_stream_attributes *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->raw, raw, size < sizeof(out->raw) ? size : sizeof(out->raw));
    if (raw[language_at] >= 'a' && raw[language_at] <= 'z' && raw[language_at + 1] >= 'a' &&
        raw[language_at + 1] <= 'z')
    {
        out->language[0] = (char)raw[language_at];
        out->language[1] = (char)raw[language_at + 1];
    }
}
