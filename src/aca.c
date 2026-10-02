#include "internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0755)
#endif
#define ACA_HEADER 32u
#define ACA_FIXED_ENTRY 46u
#define ACA_MAX_ENTRIES 4096u
#define ACA_MAX_MEMBER (128u * 1024u * 1024u)

struct hddvd_aca
{
    hddvd_stream *stream;
    hddvd_aca_entry *entries;
    size_t count;
};
static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t crc32_calc(const unsigned char *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    size_t i;
    for (i = 0; i < n; i++)
    {
        unsigned k;
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ ((0u - (c & 1u)) & 0xedb88320u);
    }
    return c ^ 0xffffffffu;
}
static int exact(hddvd_stream *s, uint64_t o, void *p, size_t n)
{
    size_t got = 0;
    int rc = hddvd_stream_read_at(s, o, p, n, &got);
    return rc ? rc : (got == n ? HDDVD_OK : HDDVD_EPARSE);
}

static int parse(hddvd_stream *s, const hddvd_limits *limits, hddvd_aca **out)
{
    unsigned char h[ACA_HEADER];
    hddvd_aca *a;
    uint64_t pos = ACA_HEADER, first = 0;
    uint16_t count;
    size_t i, j;
    int rc = exact(s, 0, h, sizeof(h));
    if (rc)
        return rc;
    if (memcmp(h, "HDDVDACA", 8))
        return HDDVD_EPARSE;
    count = be16(h + 12);
    if (!count || count > limits->max_aca_entries)
        return count ? HDDVD_ELIMIT : HDDVD_EPARSE;
    a = calloc(1, sizeof(*a));
    if (!a)
        return HDDVD_ENOMEM;
    a->entries = calloc(count, sizeof(*a->entries));
    if (!a->entries)
    {
        free(a);
        return HDDVD_ENOMEM;
    }
    a->stream = s;
    hddvd_stream_retain(s);
    a->count = count;
    for (i = 0; i < a->count; i++)
    {
        unsigned char b[14], pad[32];
        hddvd_aca_entry *e = &a->entries[i];
        uint8_t nl;
        if (pos > s->size || ACA_FIXED_ENTRY > s->size - pos)
        {
            rc = HDDVD_EPARSE;
            goto fail;
        }
        rc = exact(s, pos, b, sizeof(b));
        if (rc)
            goto fail;
        e->offset = be32(b);
        e->size = be32(b + 4);
        e->crc32 = be32(b + 8);
        e->flags = b[12];
        nl = b[13];
        if (!nl || (uint64_t)ACA_FIXED_ENTRY + nl > s->size - pos)
        {
            rc = HDDVD_EPARSE;
            goto fail;
        }
        rc = exact(s, pos + 14, e->name, nl);
        if (rc)
            goto fail;
        e->name[nl] = 0;
        rc = exact(s, pos + 14 + nl, pad, sizeof(pad));
        if (rc)
            goto fail;
        (void)pad;
        if ((uint64_t)e->offset > s->size || e->size > s->size - e->offset)
        {
            rc = HDDVD_EPARSE;
            goto fail;
        }
        if (e->size > limits->max_aca_member_size)
        {
            rc = HDDVD_ELIMIT;
            goto fail;
        }
        if (i == 0)
            first = e->offset;
        if (e->offset < first ||
            (i && e->offset < (uint64_t)a->entries[i - 1].offset + a->entries[i - 1].size))
        {
            rc = HDDVD_EPARSE;
            goto fail;
        }
        for (j = 0; j < i; j++)
            if (hddvd_ascii_equal_ci(e->name, a->entries[j].name))
            {
                rc = HDDVD_EPARSE;
                goto fail;
            }
        pos += ACA_FIXED_ENTRY + nl;
    }
    if (!first || pos != first)
    {
        rc = HDDVD_EPARSE;
        goto fail;
    }
    *out = a;
    return HDDVD_OK;
fail:
    hddvd_aca_close(a);
    return rc;
}

int hddvd_aca_supported(void)
{
    return 1;
}
void hddvd_limits_default(hddvd_limits *l)
{
    if (l)
    {
        l->max_aca_entries = ACA_MAX_ENTRIES;
        l->max_aca_member_size = ACA_MAX_MEMBER;
    }
}
int hddvd_aca_open_stream_with_limits(hddvd_stream *s, const hddvd_limits *l, hddvd_aca **out)
{
    if (!s || !l || !out || !l->max_aca_entries)
        return HDDVD_EINVAL;
    *out = NULL;
    return parse(s, l, out);
}
int hddvd_aca_open_stream_with_config(hddvd_stream *s, const hddvd_config_limits *config,
                                      hddvd_aca **out)
{
    hddvd_limits limits;
    if (!hddvd_config_limits_valid(config))
        return HDDVD_EINVAL;
    limits.max_aca_entries = config->max_archive_members;
    limits.max_aca_member_size = config->max_archive_member_bytes;
    return hddvd_aca_open_stream_with_limits(s, &limits, out);
}
int hddvd_aca_open_stream(hddvd_stream *s, hddvd_aca **out)
{
    hddvd_limits l;
    hddvd_limits_default(&l);
    return hddvd_aca_open_stream_with_limits(s, &l, out);
}
int hddvd_aca_open(const char *p, hddvd_aca **out)
{
    hddvd_stream *s;
    int rc;
    if (!p || !out)
        return HDDVD_EINVAL;
    *out = NULL;
    rc = hddvd_stream_open_file(p, &s);
    if (rc)
        return rc;
    rc = hddvd_aca_open_stream(s, out);
    hddvd_stream_close(s);
    return rc;
}
void hddvd_aca_close(hddvd_aca *a)
{
    if (a)
    {
        hddvd_stream_close(a->stream);
        free(a->entries);
        free(a);
    }
}
size_t hddvd_aca_count(const hddvd_aca *a)
{
    return a ? a->count : 0;
}
int hddvd_aca_get_entry(const hddvd_aca *a, size_t i, hddvd_aca_entry *e)
{
    if (!a || !e)
        return HDDVD_EINVAL;
    if (i >= a->count)
        return HDDVD_ENOTFOUND;
    *e = a->entries[i];
    return HDDVD_OK;
}
static const hddvd_aca_entry *find_entry(const hddvd_aca *a, const char *n)
{
    size_t i;
    for (i = 0; i < a->count; i++)
        if (hddvd_ascii_equal_ci(n, a->entries[i].name))
            return &a->entries[i];
    return NULL;
}
int hddvd_aca_open_member(const hddvd_aca *a, const char *n, hddvd_stream **out)
{
    const hddvd_aca_entry *e;
    if (!a || !n || !out)
        return HDDVD_EINVAL;
    *out = NULL;
    e = find_entry(a, n);
    return e ? hddvd_stream_slice(a->stream, e->offset, e->size, out) : HDDVD_ENOTFOUND;
}
int hddvd_aca_list(const char *p, hddvd_aca_entry_cb cb, void *o)
{
    hddvd_aca *a;
    size_t i;
    int rc = hddvd_aca_open(p, &a);
    if (rc)
        return rc;
    for (i = 0; i < a->count && cb; i++)
    {
        rc = cb(o, &a->entries[i]);
        if (rc)
            break;
    }
    hddvd_aca_close(a);
    return rc;
}
int hddvd_aca_read_member(const char *p, const char *n, void **out, size_t *sz)
{
    hddvd_aca *a;
    hddvd_stream *s;
    unsigned char *b;
    size_t got;
    int rc;
    if (!p || !n || !out || !sz)
        return HDDVD_EINVAL;
    *out = NULL;
    *sz = 0;
    rc = hddvd_aca_open(p, &a);
    if (rc)
        return rc;
    rc = hddvd_aca_open_member(a, n, &s);
    if (rc)
    {
        hddvd_aca_close(a);
        return rc;
    }
    b = malloc((size_t)s->size + 1);
    if (!b)
    {
        hddvd_stream_close(s);
        hddvd_aca_close(a);
        return HDDVD_ENOMEM;
    }
    rc = hddvd_stream_read_at(s, 0, b, (size_t)s->size, &got);
    if (!rc && got != s->size)
        rc = HDDVD_EIO;
    hddvd_stream_close(s);
    hddvd_aca_close(a);
    if (rc)
    {
        free(b);
        return rc;
    }
    b[got] = 0;
    *out = b;
    *sz = got;
    return HDDVD_OK;
}
int hddvd_aca_verify(const char *p, size_t *bad)
{
    hddvd_aca *a;
    size_t i;
    int rc;
    if (!bad)
        return HDDVD_EINVAL;
    *bad = 0;
    rc = hddvd_aca_open(p, &a);
    if (rc)
        return rc;
    for (i = 0; i < a->count; i++)
    {
        hddvd_aca_entry *e = &a->entries[i];
        unsigned char *b = malloc(e->size ? e->size : 1);
        size_t got;
        if (!b)
        {
            hddvd_aca_close(a);
            return HDDVD_ENOMEM;
        }
        rc = hddvd_stream_read_at(a->stream, e->offset, b, e->size, &got);
        if (!rc && got != e->size)
            rc = HDDVD_EIO;
        if (!rc && crc32_calc(b, got) != e->crc32)
            (*bad)++;
        free(b);
        if (rc)
        {
            hddvd_aca_close(a);
            return rc;
        }
    }
    hddvd_aca_close(a);
    return *bad ? HDDVD_EPARSE : HDDVD_OK;
}
static int safe_name(const char *n)
{
    return n && *n && !strstr(n, "..") && !strchr(n, '/') && !strchr(n, '\\');
}
typedef struct extract_ctx
{
    const char *aca, *dir;
} extract_ctx;
static int extract_cb(void *o, const hddvd_aca_entry *e)
{
    extract_ctx *c = o;
    void *b = NULL;
    size_t n = 0;
    char *path;
    FILE *f;
    int rc;
    if (!safe_name(e->name))
        return HDDVD_EPARSE;
    rc = hddvd_aca_read_member(c->aca, e->name, &b, &n);
    if (rc)
        return rc;
    path = hddvd_path_join(c->dir, e->name);
    if (!path)
    {
        free(b);
        return HDDVD_ENOMEM;
    }
    f = fopen(path, "wb");
    free(path);
    if (!f)
    {
        free(b);
        return HDDVD_EIO;
    }
    rc = n && fwrite(b, 1, n, f) != n ? HDDVD_EIO : HDDVD_OK;
    fclose(f);
    free(b);
    return rc;
}
int hddvd_aca_extract(const char *p, const char *d)
{
    extract_ctx c;
    if (!p || !d)
        return HDDVD_EINVAL;
    if (!hddvd_path_is_dir(d) && MKDIR(d) != 0)
        return HDDVD_EIO;
    c.aca = p;
    c.dir = d;
    return hddvd_aca_list(p, extract_cb, &c);
}
typedef struct visit_ctx
{
    const char *aca;
    hddvd_xmf_cb cb;
    void *o;
} visit_ctx;
static int visit_cb(void *o, const hddvd_aca_entry *e)
{
    visit_ctx *c = o;
    void *b;
    size_t n;
    int rc;
    if (!hddvd_ascii_endswith_ci(e->name, ".xmf"))
        return 0;
    rc = hddvd_aca_read_member(c->aca, e->name, &b, &n);
    if (rc)
        return rc;
    rc = c->cb(c->o, e->name, b, n);
    free(b);
    return rc;
}
int hddvd_aca_visit_xmf(const char *p, hddvd_xmf_cb cb, void *o)
{
    visit_ctx c;
    if (!p || !cb)
        return HDDVD_EINVAL;
    c.aca = p;
    c.cb = cb;
    c.o = o;
    return hddvd_aca_list(p, visit_cb, &c);
}
