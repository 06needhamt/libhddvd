/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include "hddvd.h"
#include <assert.h>
#include <stdio.h>
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

/* The original Standard Content disc in tests/fixtures/standard (see
   generate_objects.py): title 1 is PGC 1 (cells of 12 and 8 sectors, 45 and
   30 frames, a chapter at each), title 2 is PGC 2 (16 sectors, 60 frames),
   all in HV001T01.EVO. */
#define SECTOR 2048u
#define FRAME 3003u
#define COPY "titles-disc"

static unsigned char *load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long end;
    unsigned char *bytes;
    assert(f && !fseek(f, 0, SEEK_END) && (end = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET));
    bytes = malloc((size_t)end);
    assert(bytes && fread(bytes, 1, (size_t)end, f) == (size_t)end);
    fclose(f);
    *size = (size_t)end;
    return bytes;
}

static void save(const char *path, const unsigned char *bytes, size_t size)
{
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(bytes, 1, size, f) == size);
    fclose(f);
}

static unsigned char *fixture(const char *name, size_t *size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/standard/HVDVD_TS/%s", HDDVD_FIXTURE_DIR, name);
    return load(path, size);
}

static void titles_and_chapters(const hddvd_titles *titles)
{
    hddvd_title_info info;
    hddvd_title_chapter chapter;
    hddvd_title_extent extent;
    size_t index;
    assert(hddvd_titles_count(titles) == 2 && hddvd_titles_count(NULL) == 0);
    assert(hddvd_titles_get_info(titles, 0, &info) == HDDVD_OK);
    assert(info.title_set == 1 && info.title_set_title == 1 && info.angle_count == 1);
    assert(info.chapter_count == 2 && info.extent_count == 2 && info.size == 20u * SECTOR);
    assert(info.duration.ticks == 75u * FRAME);
    assert(hddvd_titles_get_chapter(titles, 0, 0, &chapter) == HDDVD_OK);
    assert(chapter.start.ticks == 0 && chapter.offset == 0);
    assert(hddvd_titles_get_chapter(titles, 0, 1, &chapter) == HDDVD_OK);
    assert(chapter.start.ticks == 45u * FRAME && chapter.offset == 12u * SECTOR);
    assert(hddvd_titles_get_extent(titles, 0, 1, &extent) == HDDVD_OK);
    assert(!strcmp(extent.evo_uri, "file:///dvddisc/HVDVD_TS/HV001T01.EVO"));
    assert(extent.first_sector == 12 && extent.sector_count == 8 && extent.offset == 12u * SECTOR &&
           extent.start.ticks == 45u * FRAME && extent.duration.ticks == 30u * FRAME);
    assert(hddvd_titles_chapter_at(titles, 0, 0, &index) == HDDVD_OK && index == 0);
    assert(hddvd_titles_chapter_at(titles, 0, 12u * SECTOR - 1, &index) == HDDVD_OK && index == 0);
    assert(hddvd_titles_chapter_at(titles, 0, 12u * SECTOR, &index) == HDDVD_OK && index == 1);
    assert(hddvd_titles_chapter_at(titles, 0, 1u << 30, &index) == HDDVD_OK && index == 1);

    assert(hddvd_titles_get_info(titles, 1, &info) == HDDVD_OK);
    assert(info.title_set_title == 2 && info.chapter_count == 1 && info.extent_count == 1);
    assert(info.size == 16u * SECTOR && info.duration.ticks == 60u * FRAME);
    assert(hddvd_titles_get_extent(titles, 1, 0, &extent) == HDDVD_OK);
    assert(extent.first_sector == 20 && extent.offset == 0 && extent.start.ticks == 0);

    assert(hddvd_titles_get_info(titles, 2, &info) == HDDVD_EINVAL);
    assert(hddvd_titles_get_info(NULL, 0, &info) == HDDVD_EINVAL);
    assert(hddvd_titles_get_info(titles, 0, NULL) == HDDVD_EINVAL);
    assert(hddvd_titles_get_chapter(titles, 0, 2, &chapter) == HDDVD_EINVAL);
    assert(hddvd_titles_get_chapter(titles, 2, 0, &chapter) == HDDVD_EINVAL);
    assert(hddvd_titles_get_extent(titles, 1, 1, &extent) == HDDVD_EINVAL);
    assert(hddvd_titles_chapter_at(titles, 2, 0, &index) == HDDVD_EINVAL);
    assert(hddvd_titles_chapter_at(titles, 0, 0, NULL) == HDDVD_EINVAL);
}

static void reads(hddvd_titles *titles)
{
    size_t evo_size, got = 1;
    unsigned char *evo = fixture("HV001T01.EVO", &evo_size);
    unsigned char *buffer = malloc(64u * SECTOR);
    assert(buffer && evo_size == 36u * SECTOR);
    /* Whole titles are their extents' sectors. */
    assert(hddvd_titles_read(titles, 0, 0, buffer, 64u * SECTOR, &got) == HDDVD_OK);
    assert(got == 20u * SECTOR && !memcmp(buffer, evo, got));
    assert(hddvd_titles_read(titles, 1, 0, buffer, 64u * SECTOR, &got) == HDDVD_OK);
    assert(got == 16u * SECTOR && !memcmp(buffer, evo + 20u * SECTOR, got));
    /* Unaligned, across the extent boundary, and short at the end. */
    assert(hddvd_titles_read(titles, 0, 12u * SECTOR - 100, buffer, 300, &got) == HDDVD_OK);
    assert(got == 300 && !memcmp(buffer, evo + 12u * SECTOR - 100, 300));
    assert(hddvd_titles_read(titles, 1, 16u * SECTOR - 10, buffer, 100, &got) == HDDVD_OK);
    assert(got == 10 && !memcmp(buffer, evo + 36u * SECTOR - 10, 10));
    assert(hddvd_titles_read(titles, 1, 16u * SECTOR, buffer, 100, &got) == HDDVD_OK && !got);
    assert(hddvd_titles_read(titles, 0, 5, buffer, 0, &got) == HDDVD_OK && !got);
    assert(hddvd_titles_read(titles, 2, 0, buffer, 1, &got) == HDDVD_EINVAL);
    assert(hddvd_titles_read(titles, 0, 0, NULL, 1, &got) == HDDVD_EINVAL);
    assert(hddvd_titles_read(titles, 0, 0, buffer, 1, NULL) == HDDVD_EINVAL);
    assert(hddvd_titles_read(NULL, 0, 0, buffer, 1, &got) == HDDVD_EINVAL && !got);
    free(buffer);
    free(evo);
}

/* A copy of the disc whose EVO is `evo` (evo_size bytes); returns the open
   status, and the titles in *out when asked. */
static int open_copy(const unsigned char *evo, size_t evo_size, hddvd_ctx **ctx, hddvd_titles **out)
{
    static const char *const ifos[] = {"HV000I01.IFO", "HV001I01.IFO"};
    size_t i, size;
    int rc;
    MKDIR(COPY);
    MKDIR(COPY "/HVDVD_TS");
    for (i = 0; i < 2; i++)
    {
        char path[64];
        unsigned char *bytes = fixture(ifos[i], &size);
        snprintf(path, sizeof(path), COPY "/HVDVD_TS/%s", ifos[i]);
        save(path, bytes, size);
        free(bytes);
    }
    remove(COPY "/HVDVD_TS/HV001T01.EVO");
    if (evo)
        save(COPY "/HVDVD_TS/HV001T01.EVO", evo, evo_size);
    *ctx = hddvd_open(COPY);
    assert(*ctx);
    rc = hddvd_titles_open(*ctx, out);
    assert(rc == HDDVD_OK || !*out);
    return rc;
}

/* Providers for the scrambled sector: one that supplies the clear sector
   (the fixture's, with the scrambling bits cleared), and ones that decline,
   fail, leave it scrambled, or return something that is not a pack. */
static unsigned provider_calls;

static int provide(void *opaque, const char *evo_uri, uint32_t sector, uint8_t data[2048])
{
    int mode = *(const int *)opaque;
    provider_calls++;
    assert(!strcmp(evo_uri, "file:///dvddisc/HVDVD_TS/HV001T01.EVO") && sector == 13);
    assert(data[3] == 0xba && (data[20] & 0x30) == 0x10);
    if (mode == 0)
        data[20] &= 0xcf;
    else if (mode == 1)
        return HDDVD_EPROTECTED;
    else if (mode == 2)
        return HDDVD_EIO;
    else if (mode == 4)
        data[3] = 0;
    return HDDVD_OK;
}

static void provider(void)
{
    size_t evo_size, got;
    unsigned char *evo = fixture("HV001T01.EVO", &evo_size);
    unsigned char *buffer = malloc(64u * SECTOR);
    hddvd_ctx *ctx;
    hddvd_titles *titles = NULL;
    hddvd_error_detail detail;
    int mode = 0;
    assert(buffer);
    evo[13u * SECTOR + 20] |= 0x10;
    assert(open_copy(evo, evo_size, &ctx, &titles) == HDDVD_OK);
    evo[13u * SECTOR + 20] &= 0xef;
    assert(hddvd_titles_set_sector_provider(titles, provide, &mode) == HDDVD_OK);
    /* The clear sector is read as if it had been stored. */
    assert(hddvd_titles_read(titles, 0, 0, buffer, 64u * SECTOR, &got) == HDDVD_OK);
    assert(got == 20u * SECTOR && !memcmp(buffer, evo, got) && provider_calls == 1);
    /* Only the scrambled sector goes to the provider. */
    assert(hddvd_titles_read(titles, 1, 0, buffer, 64u * SECTOR, &got) == HDDVD_OK);
    assert(provider_calls == 1);
    for (mode = 1; mode <= 4; mode++)
    {
        static const int expected[] = {0, HDDVD_EPROTECTED, HDDVD_EIO, HDDVD_EPROTECTED,
                                       HDDVD_EPARSE};
        assert(hddvd_titles_read(titles, 0, 0, buffer, 64u * SECTOR, &got) == expected[mode]);
        assert(got == 13u * SECTOR);
        assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == expected[mode]);
    }
    /* Without a provider again, the sector is refused. */
    assert(hddvd_titles_set_sector_provider(titles, NULL, NULL) == HDDVD_OK);
    provider_calls = 0;
    assert(hddvd_titles_read(titles, 0, 0, buffer, 64u * SECTOR, &got) == HDDVD_EPROTECTED);
    assert(provider_calls == 0);
    assert(hddvd_titles_set_sector_provider(NULL, provide, &mode) == HDDVD_EINVAL);
    hddvd_titles_close(titles);
    hddvd_close(ctx);
    free(buffer);
    free(evo);
}

static void errors(void)
{
    size_t evo_size, got;
    unsigned char *evo = fixture("HV001T01.EVO", &evo_size);
    unsigned char *buffer = malloc(64u * SECTOR);
    hddvd_ctx *ctx;
    hddvd_titles *titles = NULL;
    hddvd_error_detail detail;
    char root[512];
    assert(buffer);

    /* No VMG: an Advanced Content or unrelated directory. */
    snprintf(root, sizeof(root), "%s/app", HDDVD_FIXTURE_DIR);
    ctx = hddvd_open(root);
    assert(ctx && hddvd_titles_open(ctx, &titles) == HDDVD_ENOTFOUND && !titles);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == HDDVD_ENOTFOUND);
    assert(hddvd_titles_open(ctx, NULL) == HDDVD_EINVAL);
    assert(hddvd_titles_open(NULL, &titles) == HDDVD_EINVAL);
    hddvd_close(ctx);

    /* No EVO, or one too short for its cells. */
    assert(open_copy(NULL, 0, &ctx, &titles) == HDDVD_ENOTFOUND);
    hddvd_close(ctx);
    assert(open_copy(evo, 30u * SECTOR, &ctx, &titles) == HDDVD_EPARSE);
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == HDDVD_EPARSE);
    hddvd_close(ctx);

    /* A scrambled video packet in sector 13 (the pack header is 14 bytes,
       then the PES header's flags): reading stops before it. */
    evo[13u * SECTOR + 20] |= 0x10;
    assert(open_copy(evo, evo_size, &ctx, &titles) == HDDVD_OK);
    assert(hddvd_titles_read(titles, 0, 0, buffer, 64u * SECTOR, &got) == HDDVD_EPROTECTED);
    assert(got == 13u * SECTOR && !memcmp(buffer, evo, got));
    assert(hddvd_get_last_error(ctx, &detail) == HDDVD_OK && detail.status == HDDVD_EPROTECTED);
    assert(hddvd_titles_read(titles, 1, 0, buffer, 64u * SECTOR, &got) == HDDVD_OK);
    hddvd_titles_close(titles);
    hddvd_close(ctx);
    evo[13u * SECTOR + 20] &= 0xef;

    /* A sector that is not a pack. */
    evo[21u * SECTOR + 3] = 0xbb;
    assert(open_copy(evo, evo_size, &ctx, &titles) == HDDVD_OK);
    assert(hddvd_titles_read(titles, 1, SECTOR + 7, buffer, 64u * SECTOR, &got) == HDDVD_EPARSE);
    assert(got == 0);
    hddvd_titles_close(titles);
    hddvd_close(ctx);
    hddvd_titles_close(NULL);
    free(buffer);
    free(evo);
}

int main(void)
{
    char root[512];
    hddvd_ctx *ctx;
    hddvd_titles *titles = NULL;
    snprintf(root, sizeof(root), "%s/standard", HDDVD_FIXTURE_DIR);
    ctx = hddvd_open(root);
    assert(ctx && hddvd_titles_open(ctx, &titles) == HDDVD_OK);
    titles_and_chapters(titles);
    reads(titles);
    hddvd_titles_close(titles);
    hddvd_close(ctx);
    errors();
    provider();
    return 0;
}
