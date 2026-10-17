#include "hddvd.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read-only byte accounting for script objects (task P4-01). Every input byte
   is reported exactly once as one of:
     structure  a field of a format this project documents and validates
                (docs/formats/aacs-object-envelope.md, docs/formats/hdi-script.md);
     observed   fields seen consistently in external media whose meaning is
                unconfirmed (AACS bytes 5-6 and clear-type trailers);
     unknown    everything else, including unreferenced gaps.
   Tables are bounds-checked by the library parsers before they are labelled.
   No opcode or field meaning beyond those notes is assumed. */

typedef enum region_kind
{
    REGION_STRUCTURE,
    REGION_OBSERVED,
    REGION_UNKNOWN
} region_kind;

typedef struct region
{
    uint64_t offset;
    uint64_t length;
    region_kind kind;
    char label[64];
} region;

typedef struct layout
{
    region *items;
    size_t count;
    size_t capacity;
    int failed;
} layout;

static const char *const kind_names[] = {"structure", "observed", "unknown"};

static void add(layout *l, uint64_t offset, uint64_t length, region_kind kind, const char *label)
{
    region *grown;
    if (!length || l->failed)
        return;
    if (l->count == l->capacity)
    {
        size_t capacity = l->capacity ? l->capacity * 2 : 32;
        grown = realloc(l->items, capacity * sizeof(*grown));
        if (!grown)
        {
            l->failed = 1;
            return;
        }
        l->items = grown;
        l->capacity = capacity;
    }
    l->items[l->count].offset = offset;
    l->items[l->count].length = length;
    l->items[l->count].kind = kind;
    snprintf(l->items[l->count].label, sizeof(l->items[l->count].label), "%s", label);
    l->count++;
}

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}
static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Labels a clean-room HDIS script that hddvd_script_open() has accepted, so
   all offsets and counts below are already bounds-checked. */
static int account_hdis(layout *l, const unsigned char *p, size_t size, uint64_t base,
                        const char *prefix)
{
    static const struct
    {
        size_t offset, length;
        const char *name;
    } fields[] = {
        {0, 4, "magic"},          {4, 2, "version"},          {6, 2, "header_size"},
        {8, 4, "string_count"},   {12, 4, "function_count"},  {16, 4, "instruction_count"},
        {20, 4, "string_offset"}, {24, 4, "function_offset"}, {28, 4, "instruction_offset"}};
    hddvd_script *script = NULL;
    char label[64];
    uint32_t i, sc, fc, ic, so, fo, io;
    uint64_t cursor;
    int rc = hddvd_script_open(p, size, &script);
    if (rc != HDDVD_OK)
        return rc;
    hddvd_script_close(script);
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
    {
        snprintf(label, sizeof(label), "%sscript.%s", prefix, fields[i].name);
        add(l, base + fields[i].offset, fields[i].length, REGION_STRUCTURE, label);
    }
    sc = be32(p + 8);
    fc = be32(p + 12);
    ic = be32(p + 16);
    so = be32(p + 20);
    fo = be32(p + 24);
    io = be32(p + 28);
    cursor = so;
    for (i = 0; i < sc; i++)
    {
        uint32_t n = be32(p + cursor);
        snprintf(label, sizeof(label), "%sscript.string[%u]", prefix, (unsigned)i);
        add(l, base + cursor, 4u + (uint64_t)n, REGION_STRUCTURE, label);
        cursor += 4u + (uint64_t)n;
    }
    snprintf(label, sizeof(label), "%sscript.functions[%u]", prefix, (unsigned)fc);
    add(l, base + fo, (uint64_t)fc * 12u, REGION_STRUCTURE, label);
    snprintf(label, sizeof(label), "%sscript.instructions[%u]", prefix, (unsigned)ic);
    add(l, base + io, (uint64_t)ic * 8u, REGION_STRUCTURE, label);
    return HDDVD_OK;
}

static int account_object(layout *l, const unsigned char *p, size_t size)
{
    static const struct
    {
        size_t offset, length;
        region_kind kind;
        const char *name;
    } fields[] = {{0, 8, REGION_STRUCTURE, "object.magic"},
                  {8, 1, REGION_STRUCTURE, "object.version"},
                  {9, 1, REGION_STRUCTURE, "object.flags"},
                  {10, 2, REGION_STRUCTURE, "object.type"},
                  {12, 4, REGION_STRUCTURE, "object.header_length"},
                  {16, 8, REGION_STRUCTURE, "object.payload_length"},
                  {24, 2, REGION_STRUCTURE, "object.name_length"},
                  {26, 2, REGION_STRUCTURE, "object.trailer_length"},
                  {28, 4, REGION_UNKNOWN, "object.unknown_1"}};
    hddvd_object *object = NULL;
    hddvd_object_info info;
    size_t i, name_length;
    int rc = hddvd_object_open(p, size, &object);
    if (rc != HDDVD_OK && rc != HDDVD_EPROTECTED)
        return rc;
    hddvd_object_close(object);
    /* The parser validated framing before reporting protection, so the
       header fields are consistent with the input size either way. */
    memset(&info, 0, sizeof(info));
    info.flags = p[9];
    info.payload_offset = be32(p + 12);
    info.payload_length = (uint64_t)size - info.payload_offset - be16(p + 26);
    info.trailer_offset = info.payload_offset + info.payload_length;
    info.trailer_length = be16(p + 26);
    name_length = be16(p + 24);
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
        add(l, fields[i].offset, fields[i].length, fields[i].kind, fields[i].name);
    add(l, 32, name_length, REGION_STRUCTURE, "object.name");
    add(l, 32 + name_length, info.payload_offset - 32 - name_length, REGION_STRUCTURE,
        "object.padding");
    if (rc == HDDVD_EPROTECTED)
        add(l, info.payload_offset, info.payload_length, REGION_UNKNOWN,
            "object.payload (protected)");
    else if (info.payload_length >= 4 && !memcmp(p + info.payload_offset, "HDIS", 4) &&
             account_hdis(l, p + info.payload_offset, (size_t)info.payload_length,
                          info.payload_offset, "payload.") == HDDVD_OK)
        ;
    else
        add(l, info.payload_offset, info.payload_length, REGION_UNKNOWN, "object.payload");
    add(l, info.trailer_offset, info.trailer_length, REGION_UNKNOWN, "object.trailer");
    return HDDVD_OK;
}

/* Labels the on-disc AACS envelope (docs/formats/aacs-object-envelope.md)
   after the library has validated it. Bytes 5-6 and clear-type trailers have
   a consistent size but no established meaning, so they are "observed". */
static int account_aacs(layout *l, const unsigned char *p, size_t size)
{
    hddvd_object *object = NULL;
    hddvd_object_info info;
    int rc = hddvd_object_open(p, size, &object);
    int protected_object = rc == HDDVD_EPROTECTED;
    size_t name_length = 0;
    if (rc != HDDVD_OK && !protected_object)
        return rc;
    if (object)
        name_length = strlen(hddvd_object_name(object));
    hddvd_object_close(object);
    memset(&info, 0, sizeof(info));
    info.payload_length = (uint64_t)p[7] << 24 | (uint64_t)p[8] << 16 | (uint64_t)p[9] << 8 | p[10];
    info.trailer_offset = 283u + info.payload_length;
    add(l, 0, 4, REGION_STRUCTURE, "aacs.magic");
    add(l, 4, 1, REGION_STRUCTURE, "aacs.type");
    add(l, 5, 2, REGION_OBSERVED, "aacs.bytes_5_6");
    add(l, 7, 4, REGION_STRUCTURE, "aacs.payload_length");
    if (protected_object)
    {
        add(l, 11, 283 - 11, REGION_UNKNOWN, "aacs.header (protected)");
        add(l, 283, info.payload_length, REGION_UNKNOWN, "aacs.payload (protected)");
        add(l, info.trailer_offset, size - info.trailer_offset, REGION_UNKNOWN, "aacs.trailer");
        return HDDVD_OK;
    }
    add(l, 11, name_length, REGION_STRUCTURE, "aacs.name");
    add(l, 11 + name_length, 283 - 11 - name_length, REGION_STRUCTURE, "aacs.padding");
    if (info.payload_length >= 4 && !memcmp(p + 283, "HDIS", 4) &&
        account_hdis(l, p + 283, (size_t)info.payload_length, 283, "payload.") == HDDVD_OK)
        ;
    else
        add(l, 283, info.payload_length, REGION_UNKNOWN, "aacs.payload");
    add(l, info.trailer_offset, size - info.trailer_offset, REGION_OBSERVED, "aacs.trailer");
    return HDDVD_OK;
}

static int compare_regions(const void *a, const void *b)
{
    const region *left = a, *right = b;
    return left->offset < right->offset ? -1 : left->offset > right->offset;
}

int main(int argc, char **argv)
{
    FILE *f;
    long end;
    unsigned char *bytes;
    size_t size, i;
    layout l = {NULL, 0, 0, 0};
    const char *format = "unrecognized";
    uint64_t cursor = 0, totals[3] = {0, 0, 0};
    int rc = HDDVD_OK;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s SCRIPT\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f || fseek(f, 0, SEEK_END) || (end = ftell(f)) < 0 || fseek(f, 0, SEEK_SET))
    {
        fprintf(stderr, "%s: cannot read input\n", argv[0]);
        if (f)
            fclose(f);
        return 1;
    }
    size = (size_t)end;
    bytes = malloc(size ? size : 1);
    if (!bytes || fread(bytes, 1, size, f) != size)
    {
        fprintf(stderr, "%s: cannot read input\n", argv[0]);
        free(bytes);
        fclose(f);
        return 1;
    }
    fclose(f);

    if (size >= 8 && !memcmp(bytes, "HDDVDOBJ", 8))
    {
        format = "object-envelope";
        rc = account_object(&l, bytes, size);
    }
    else if (size >= 4 && !memcmp(bytes, "HDIS", 4))
    {
        format = "clean-room-script";
        rc = account_hdis(&l, bytes, size, 0, "");
    }
    else if (size >= 4 && !memcmp(bytes, "AACS", 4))
    {
        format = "aacs-envelope";
        rc = account_aacs(&l, bytes, size);
    }
    free(bytes);
    if (rc != HDDVD_OK)
    {
        fprintf(stderr, "%s: invalid %s: %s\n", argv[0], format, hddvd_strerror(rc));
        free(l.items);
        return 1;
    }
    if (l.failed)
    {
        fprintf(stderr, "%s: out of memory\n", argv[0]);
        free(l.items);
        return 1;
    }

    /* qsort must not be given a NULL array, even an empty one. */
    if (l.count)
        qsort(l.items, l.count, sizeof(*l.items), compare_regions);
    printf("format: %s\nsize: %zu\n", format, size);
    for (i = 0; i <= l.count; i++)
    {
        uint64_t start = i < l.count ? l.items[i].offset : size;
        if (start > cursor)
        {
            printf("0x%08llx %10llu  unknown    unreferenced\n", (unsigned long long)cursor,
                   (unsigned long long)(start - cursor));
            totals[REGION_UNKNOWN] += start - cursor;
            cursor = start;
        }
        if (i == l.count)
            break;
        if (start < cursor)
        {
            /* The decoder permits tables to share bytes; count them once. */
            uint64_t overlap = cursor - start;
            printf("overlap: %s shares %llu bytes with the preceding region\n", l.items[i].label,
                   (unsigned long long)(overlap < l.items[i].length ? overlap : l.items[i].length));
            if (overlap >= l.items[i].length)
                continue;
            l.items[i].offset = cursor;
            l.items[i].length -= overlap;
        }
        printf("0x%08llx %10llu  %-9s  %s\n", (unsigned long long)l.items[i].offset,
               (unsigned long long)l.items[i].length, kind_names[l.items[i].kind],
               l.items[i].label);
        totals[l.items[i].kind] += l.items[i].length;
        cursor = l.items[i].offset + l.items[i].length;
    }
    free(l.items);
    printf("summary: size=%zu structure=%llu observed=%llu unknown=%llu\n", size,
           (unsigned long long)totals[REGION_STRUCTURE],
           (unsigned long long)totals[REGION_OBSERVED], (unsigned long long)totals[REGION_UNKNOWN]);
    if (totals[REGION_STRUCTURE] + totals[REGION_OBSERVED] + totals[REGION_UNKNOWN] != size)
    {
        fprintf(stderr, "%s: internal accounting error\n", argv[0]);
        return 1;
    }
    return 0;
}
