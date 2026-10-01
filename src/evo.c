#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct hddvd_evo_parser
{
    unsigned char *data;
    size_t size, cap;
    hddvd_stream_info streams[256];
    size_t stream_count;
    int finished, failed;
};
static hddvd_stream_codec classify(unsigned sid, unsigned sub)
{
    if (sid == 0xe0) {
        return HDDVD_CODEC_MPEG2_VIDEO;
    }
    if (sid == 0xe2) {
        return HDDVD_CODEC_H264;
    }
    if (sid == 0xc0) {
        return HDDVD_CODEC_MPEG_AUDIO;
    }
    if (sid == 0xfd && sub >= 0x55 && sub <= 0x5f) {
        return HDDVD_CODEC_VC1;
    }
    if (sid == 0xbd)
    {
        if (sub >= 0x20 && sub <= 0x3f) {
            return HDDVD_CODEC_SUBPICTURE;
        }
        if (sub >= 0x88 && sub <= 0x9f) {
            return HDDVD_CODEC_DTS_HD;
        }
        if (sub >= 0xa0 && sub <= 0xaf) {
            return HDDVD_CODEC_LPCM;
        }
        if (sub >= 0xb0 && sub <= 0xbf) {
            return HDDVD_CODEC_TRUEHD_MLP;
        }
        if (sub >= 0xc0 && sub <= 0xcf) {
            return HDDVD_CODEC_AC3_EAC3;
        }
    }
    return HDDVD_CODEC_UNKNOWN;
}
const char *hddvd_codec_name(hddvd_stream_codec c)
{
    switch (c)
    {
        case HDDVD_CODEC_MPEG2_VIDEO:
            return "MPEG-2 video";
        case HDDVD_CODEC_H264:
            return "H.264/AVC";
        case HDDVD_CODEC_VC1:
            return "VC-1";
        case HDDVD_CODEC_MPEG_AUDIO:
            return "MPEG audio";
        case HDDVD_CODEC_AC3_EAC3:
            return "AC-3/E-AC-3";
        case HDDVD_CODEC_DTS_HD:
            return "DTS-HD";
        case HDDVD_CODEC_LPCM:
            return "LPCM";
        case HDDVD_CODEC_TRUEHD_MLP:
            return "MLP/TrueHD";
        case HDDVD_CODEC_SUBPICTURE:
            return "subpicture";
        default:
            return "unknown";
    }
}
static void add_stream(hddvd_evo_parser *p, unsigned sid, unsigned sub)
{
    size_t i;
    for (i = 0; i < p->stream_count; i++)
        if (p->streams[i].stream_id == sid && p->streams[i].substream_id == sub)
        {
            p->streams[i].packet_count++;
            return;
        }
    if (p->stream_count < 256)
    {
        hddvd_stream_info *s = &p->streams[p->stream_count++];
        s->stream_id = (uint8_t)sid;
        s->substream_id = (uint8_t)sub;
        s->codec = classify(sid, sub);
        s->packet_count = 1;
    }
}
static void consume(hddvd_evo_parser *p, size_t n)
{
    p->size -= n;
    memmove(p->data, p->data + n, p->size);
}
static int parse(hddvd_evo_parser *p, int final)
{
    while (p->size >= 4)
    {
        size_t i = 0, total, payload;
        unsigned sid, sub = 0;
        while (i + 3 < p->size && (p->data[i] || p->data[i + 1] || p->data[i + 2] != 1))
            i++;
        if (i)
        {
            consume(p, i);
            continue;
        }
        if (p->size < 6) {
            break;
        }
        sid = p->data[3];
        if (sid == 0xb9)
        {
            consume(p, 4);
            continue;
        }
        if (sid == 0xba)
        {
            if (p->size < 14) {
                break;
            }
            total = 14u + (size_t)(p->data[13] & 7u);
            if (p->size < total) {
                break;
            }
            consume(p, total);
            continue;
        }
        total = 6u + ((size_t)p->data[4] << 8) + (size_t)p->data[5];
        if (total == 6u)
        {
            size_t next = 6;
            while (next + 3 < p->size &&
                   (p->data[next] || p->data[next + 1] || p->data[next + 2] != 1))
                next++;
            if (next + 3 >= p->size)
            {
                if (!final) {
                    break;
                }
                total = p->size;
            }
            else
                total = next;
        }
        if (p->size < total) {
            break;
        }
        if (sid != 0xbb && sid != 0xbe && sid != 0xbf)
        {
            payload = 6;
            if (total >= 9)
            {
                size_t header = 9u + (size_t)p->data[8];
                if (header > total)
                {
                    p->failed = 1;
                    return HDDVD_EPARSE;
                }
                payload = header;
            }
            if ((sid == 0xbd || sid == 0xfd) && payload < total) {
                sub = (unsigned)p->data[payload];
            }
            add_stream(p, sid, sub);
        }
        consume(p, total);
    }
    if (final && p->size)
    {
        size_t i;
        for (i = 0; i < p->size; i++)
            if (p->data[i])
            {
                p->failed = 1;
                return HDDVD_EPARSE;
            }
        p->size = 0;
    }
    return HDDVD_OK;
}
int hddvd_evo_parser_create(hddvd_evo_parser **out)
{
    if (!out) {
        return HDDVD_EINVAL;
    }
    *out = (hddvd_evo_parser *)calloc(1, sizeof(**out));
    return *out ? HDDVD_OK : HDDVD_ENOMEM;
}
void hddvd_evo_parser_destroy(hddvd_evo_parser *p)
{
    if (p)
    {
        free(p->data);
        free(p);
    }
}
int hddvd_evo_parser_push(hddvd_evo_parser *p, const void *data, size_t size)
{
    size_t need, cap;
    unsigned char *n;
    if (!p || (!data && size) || p->finished || p->failed) {
        return HDDVD_EINVAL;
    }
    if (size > SIZE_MAX - p->size) {
        return HDDVD_ELIMIT;
    }
    need = p->size + size;
    if (need > p->cap)
    {
        cap = p->cap ? p->cap : 4096;
        while (cap < need)
        {
            if (cap > SIZE_MAX / 2) {
                return HDDVD_ELIMIT;
            }
            cap *= 2;
        }
        n = (unsigned char *)realloc(p->data, cap);
        if (!n) {
            return HDDVD_ENOMEM;
        }
        p->data = n;
        p->cap = cap;
    }
    if (size) {
        memcpy(p->data + p->size, data, size);
    }
    p->size = need;
    return parse(p, 0);
}
int hddvd_evo_parser_finish(hddvd_evo_parser *p)
{
    int r;
    if (!p || p->finished) {
        return HDDVD_EINVAL;
    }
    r = parse(p, 1);
    p->finished = 1;
    return r;
}
size_t hddvd_evo_parser_stream_count(const hddvd_evo_parser *p)
{
    return p ? p->stream_count : 0;
}
int hddvd_evo_parser_get_stream(const hddvd_evo_parser *p, size_t i, hddvd_stream_info *out)
{
    if (!p || !out || i >= p->stream_count) {
        return HDDVD_EINVAL;
    }
    *out = p->streams[i];
    return HDDVD_OK;
}
int hddvd_evo_probe(const char *path, hddvd_stream_info *out, size_t cap, size_t *count)
{
    FILE *f;
    unsigned char b[4096];
    size_t got, i, n;
    int r;
    hddvd_evo_parser *p;
    if (!path || !count || (cap && !out)) {
        return HDDVD_EINVAL;
    }
    *count = 0;
    f = fopen(path, "rb");
    if (!f) {
        return HDDVD_EIO;
    }
    r = hddvd_evo_parser_create(&p);
    if (r)
    {
        fclose(f);
        return r;
    }
    while ((got = fread(b, 1, sizeof(b), f)) != 0)
        if ((r = hddvd_evo_parser_push(p, b, got)) != 0) {
            break;
        }
    if (ferror(f) && r == 0) {
        r = HDDVD_EIO;
    }
    fclose(f);
    if (r == 0) {
        r = hddvd_evo_parser_finish(p);
    }
    n = hddvd_evo_parser_stream_count(p);
    *count = n;
    for (i = 0; i < n && i < cap; i++)
        hddvd_evo_parser_get_stream(p, i, &out[i]);
    hddvd_evo_parser_destroy(p);
    return r;
}
