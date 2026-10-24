#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Playable titles: the VMG's title table joined to each title set's
   chapters, PGCs and cells, and the cells' sectors in the title set's EVO
   (docs/formats/ifo-map.md). */
#define SECTOR 2048u
#define MAX_TITLE_SET 999u

typedef struct title_entry
{
    hddvd_title_info info;
    size_t set;
    hddvd_title_extent *extents;
    hddvd_title_chapter *chapters;
} title_entry;

typedef struct title_set
{
    uint16_t number;
    char *evo_uri;
    hddvd_stream *evo;
} title_set;

struct hddvd_titles
{
    hddvd_ctx *ctx;
    title_entry *titles;
    size_t count;
    title_set *sets;
    size_t set_count;
};

void hddvd_titles_close(hddvd_titles *titles)
{
    size_t i;
    if (!titles)
        return;
    for (i = 0; i < titles->count; i++)
    {
        free(titles->titles[i].extents);
        free(titles->titles[i].chapters);
    }
    for (i = 0; i < titles->set_count; i++)
    {
        free(titles->sets[i].evo_uri);
        hddvd_stream_close(titles->sets[i].evo);
    }
    free(titles->titles);
    free(titles->sets);
    free(titles);
}

static int fail(const hddvd_ctx *ctx, int rc, const char *uri, const char *message)
{
    hddvd_ctx_set_error(ctx, rc, HDDVD_SUBSYSTEM_DISC, uri, message);
    return rc;
}

/* Opens title set `number`: its IFO, and the EVO its cells must fit in. */
static int open_set(hddvd_titles *titles, uint16_t number, hddvd_vts **vts, title_set *set)
{
    char uri[64], relative[32], *path = NULL;
    hddvd_stream *stream = NULL;
    int rc;
    snprintf(uri, sizeof(uri), "file:///dvddisc/HVDVD_TS/HV%03uI01.IFO", (unsigned)number);
    rc = hddvd_open_resource(titles->ctx, uri, &stream);
    if (rc)
        return rc;
    rc = hddvd_vts_open_with_config(stream, &titles->ctx->limits, vts);
    hddvd_stream_close(stream);
    if (rc)
        return fail(titles->ctx, rc, uri, "invalid title set");
    snprintf(relative, sizeof(relative), "HVDVD_TS/HV%03uT01.EVO", (unsigned)number);
    snprintf(uri, sizeof(uri), "file:///dvddisc/%s", relative);
    set->number = number;
    set->evo_uri = hddvd_strdup(uri);
    if (!set->evo_uri)
        return HDDVD_ENOMEM;
    rc = hddvd_resolve_disc_path(titles->ctx, relative, &path);
    if (!rc)
        rc = hddvd_stream_open_file(path, &set->evo);
    free(path);
    if (rc)
        return fail(titles->ctx, rc, uri, "title set EVO not found");
    return HDDVD_OK;
}

/* Adds pgc's cells to the title; *first is the index of its first extent. */
static int add_pgc(hddvd_titles *titles, title_entry *t, const hddvd_vts *vts, size_t pgc,
                   const title_set *set, size_t *capacity, size_t *first)
{
    hddvd_vts_pgc info;
    size_t i;
    uint64_t evo_sectors = hddvd_stream_size(set->evo) / SECTOR;
    if (hddvd_vts_get_pgc(vts, pgc, &info))
        return fail(titles->ctx, HDDVD_EPARSE, set->evo_uri, "chapter names a missing PGC");
    *first = t->info.extent_count;
    for (i = 0; i < info.cell_count; i++)
    {
        hddvd_vts_cell cell;
        hddvd_title_extent *e;
        hddvd_time duration;
        if (hddvd_vts_get_cell(vts, pgc, i, &cell) ||
            hddvd_vts_time_to_ticks(&cell.playback_time, &duration))
            return fail(titles->ctx, HDDVD_EPARSE, set->evo_uri, "invalid cell");
        if (cell.last_sector < cell.first_vobu_sector || cell.last_sector >= evo_sectors)
            return fail(titles->ctx, HDDVD_EPARSE, set->evo_uri, "cell outside the EVO");
        if (t->info.extent_count == *capacity)
        {
            size_t grown = *capacity ? *capacity * 2 : 8;
            hddvd_title_extent *more = realloc(t->extents, grown * sizeof(*more));
            if (!more)
                return HDDVD_ENOMEM;
            memset(more + *capacity, 0, (grown - *capacity) * sizeof(*more));
            t->extents = more;
            *capacity = grown;
        }
        e = &t->extents[t->info.extent_count++];
        e->evo_uri = set->evo_uri;
        e->first_sector = cell.first_vobu_sector;
        e->sector_count = cell.last_sector - cell.first_vobu_sector + 1;
        e->offset = t->info.size;
        e->start = t->info.duration;
        e->duration = duration;
        t->info.size += (uint64_t)e->sector_count * SECTOR;
        t->info.duration.ticks += duration.ticks;
    }
    return HDDVD_OK;
}

/* Builds a title from its chapters: the PGCs they name, in the order they
   first appear. */
static int build_title(hddvd_titles *titles, title_entry *t, const hddvd_vts *vts, size_t title,
                       const title_set *set)
{
    size_t count = hddvd_vts_chapter_count(vts, title), i, j, capacity = 0;
    uint16_t *pgcs;
    size_t *firsts, pgc_count = 0;
    int rc = HDDVD_OK;
    if (!count)
        return fail(titles->ctx, HDDVD_EPARSE, set->evo_uri, "title has no chapters");
    pgcs = malloc(count * sizeof(*pgcs));
    firsts = malloc(count * sizeof(*firsts));
    t->chapters = calloc(count, sizeof(*t->chapters));
    if (!pgcs || !firsts || !t->chapters)
        rc = HDDVD_ENOMEM;
    for (i = 0; !rc && i < count; i++)
    {
        hddvd_vts_chapter chapter;
        uint16_t cell;
        size_t extent;
        if (hddvd_vts_get_chapter(vts, title, i, &chapter) || !chapter.pgc || !chapter.program ||
            hddvd_vts_get_program(vts, (size_t)chapter.pgc - 1, (size_t)chapter.program - 1,
                                  &cell) ||
            !cell)
        {
            rc = fail(titles->ctx, HDDVD_EPARSE, set->evo_uri, "invalid chapter");
            break;
        }
        for (j = 0; j < pgc_count && pgcs[j] != chapter.pgc; j++)
            ;
        if (j == pgc_count)
        {
            pgcs[pgc_count] = chapter.pgc;
            rc = add_pgc(titles, t, vts, (size_t)chapter.pgc - 1, set, &capacity,
                         &firsts[pgc_count]);
            pgc_count++;
            if (rc)
                break;
        }
        extent = firsts[j] + cell - 1u;
        if (extent >= t->info.extent_count)
        {
            rc = fail(titles->ctx, HDDVD_EPARSE, set->evo_uri, "chapter starts past its PGC");
            break;
        }
        t->chapters[i].start = t->extents[extent].start;
        t->chapters[i].offset = t->extents[extent].offset;
    }
    if (!rc)
        t->info.chapter_count = count;
    free(pgcs);
    free(firsts);
    return rc;
}

int hddvd_titles_open(hddvd_ctx *ctx, hddvd_titles **out)
{
    static const char vmg_uri[] = "file:///dvddisc/HVDVD_TS/HV000I01.IFO";
    hddvd_titles *titles;
    hddvd_stream *stream = NULL;
    hddvd_vmg *vmg = NULL;
    hddvd_vts **vts = NULL;
    size_t i, count;
    int rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!ctx)
        return HDDVD_EINVAL;
    rc = hddvd_open_resource(ctx, vmg_uri, &stream);
    if (rc == HDDVD_ENOTFOUND)
        return fail(ctx, rc, vmg_uri,
                    "no Standard Content title table (Advanced Content titles are not listed)");
    if (rc)
        return rc;
    rc = hddvd_vmg_open_with_config(stream, &ctx->limits, &vmg);
    hddvd_stream_close(stream);
    if (rc)
        return fail(ctx, rc, vmg_uri, "invalid title table");
    count = hddvd_vmg_title_count(vmg);
    titles = calloc(1, sizeof(*titles));
    if (titles)
    {
        titles->ctx = ctx;
        titles->titles = calloc(count, sizeof(*titles->titles));
        titles->sets = calloc(count, sizeof(*titles->sets));
        vts = calloc(count, sizeof(*vts));
    }
    if (!titles || !titles->titles || !titles->sets || !vts)
        rc = HDDVD_ENOMEM;
    if (!rc)
        titles->count = count;
    for (i = 0; !rc && i < count; i++)
    {
        hddvd_vmg_title title;
        title_entry *t = &titles->titles[i];
        size_t s;
        if (hddvd_vmg_get_title(vmg, i, &title) || title.title_set > MAX_TITLE_SET)
        {
            rc = fail(ctx, HDDVD_EPARSE, vmg_uri, "invalid title table entry");
            break;
        }
        for (s = 0; s < titles->set_count && titles->sets[s].number != title.title_set; s++)
            ;
        if (s == titles->set_count)
        {
            titles->set_count++;
            rc = open_set(titles, title.title_set, &vts[s], &titles->sets[s]);
            if (rc)
                break;
        }
        if (title.title_set_title > hddvd_vts_title_count(vts[s]))
        {
            rc = fail(ctx, HDDVD_EPARSE, titles->sets[s].evo_uri, "title not in its title set");
            break;
        }
        t->set = s;
        t->info.title_set = title.title_set;
        t->info.title_set_title = title.title_set_title;
        t->info.angle_count = title.angle_count;
        rc = build_title(titles, t, vts[s], (size_t)title.title_set_title - 1, &titles->sets[s]);
    }
    if (vts)
        for (i = 0; i < count; i++)
            hddvd_vts_close(vts[i]);
    free(vts);
    hddvd_vmg_close(vmg);
    if (rc)
    {
        hddvd_titles_close(titles);
        return rc;
    }
    hddvd_clear_error(ctx);
    *out = titles;
    return HDDVD_OK;
}

size_t hddvd_titles_count(const hddvd_titles *titles)
{
    return titles ? titles->count : 0;
}

int hddvd_titles_get_info(const hddvd_titles *titles, size_t title, hddvd_title_info *out)
{
    if (!titles || !out || title >= titles->count)
        return HDDVD_EINVAL;
    *out = titles->titles[title].info;
    return HDDVD_OK;
}

int hddvd_titles_get_chapter(const hddvd_titles *titles, size_t title, size_t chapter,
                             hddvd_title_chapter *out)
{
    if (!titles || !out || title >= titles->count ||
        chapter >= titles->titles[title].info.chapter_count)
        return HDDVD_EINVAL;
    *out = titles->titles[title].chapters[chapter];
    return HDDVD_OK;
}

int hddvd_titles_get_extent(const hddvd_titles *titles, size_t title, size_t extent,
                            hddvd_title_extent *out)
{
    if (!titles || !out || title >= titles->count ||
        extent >= titles->titles[title].info.extent_count)
        return HDDVD_EINVAL;
    *out = titles->titles[title].extents[extent];
    return HDDVD_OK;
}

int hddvd_titles_chapter_at(const hddvd_titles *titles, size_t title, uint64_t offset,
                            size_t *chapter)
{
    const title_entry *t;
    size_t i, best = 0;
    if (!titles || !chapter || title >= titles->count)
        return HDDVD_EINVAL;
    t = &titles->titles[title];
    /* The chapter that starts last at or before offset. */
    for (i = 1; i < t->info.chapter_count; i++)
        if (t->chapters[i].offset <= offset && t->chapters[i].offset >= t->chapters[best].offset)
            best = i;
    *chapter = best;
    return HDDVD_OK;
}

/* A sector must be a program stream pack. A PES packet whose scrambling
   bits are set marks encrypted content (docs/formats/ifo-map.md). */
static int check_sector(const unsigned char *b)
{
    size_t at;
    if (b[0] || b[1] || b[2] != 1 || b[3] != 0xba || (b[4] & 0xc0) != 0x40)
        return HDDVD_EPARSE;
    at = 14u + (b[13] & 7u);
    while (at + 7 <= SECTOR && !b[at] && !b[at + 1] && b[at + 2] == 1)
    {
        unsigned id = b[at + 3];
        size_t length = (size_t)b[at + 4] << 8 | b[at + 5];
        if ((id == 0xbd || (id >= 0xc0 && id <= 0xef) || id == 0xfd) &&
            (b[at + 6] & 0xc0) == 0x80 && (b[at + 6] & 0x30))
            return HDDVD_EPROTECTED;
        at += 6 + length;
    }
    return HDDVD_OK;
}

int hddvd_titles_read(hddvd_titles *titles, size_t title, uint64_t offset, void *buffer,
                      size_t size, size_t *got)
{
    const title_entry *t;
    unsigned char sector[SECTOR], *out = buffer;
    size_t lo, hi, done = 0;
    if (got)
        *got = 0;
    if (!titles || !got || (!buffer && size) || title >= titles->count)
        return HDDVD_EINVAL;
    t = &titles->titles[title];
    if (offset >= t->info.size || !size)
        return HDDVD_OK;
    /* The last extent starting at or before offset. */
    lo = 0;
    hi = t->info.extent_count;
    while (hi - lo > 1)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (t->extents[mid].offset <= offset)
            lo = mid;
        else
            hi = mid;
    }
    while (done < size && lo < t->info.extent_count)
    {
        const hddvd_title_extent *e = &t->extents[lo];
        uint64_t within = offset - e->offset, at;
        size_t n = 0, skip = (size_t)(within % SECTOR), take = SECTOR - skip;
        hddvd_stream *evo = titles->sets[t->set].evo;
        int rc;
        if (within >= (uint64_t)e->sector_count * SECTOR)
        {
            lo++;
            continue;
        }
        at = ((uint64_t)e->first_sector + within / SECTOR) * SECTOR;
        rc = hddvd_stream_read_at(evo, at, sector, SECTOR, &n);
        if (!rc && n != SECTOR)
            rc = HDDVD_EPARSE;
        if (!rc)
            rc = check_sector(sector);
        if (rc)
        {
            *got = done;
            return fail(titles->ctx, rc, e->evo_uri,
                        rc == HDDVD_EPROTECTED ? "encrypted EVO sector"
                                               : "EVO sector is not a program stream pack");
        }
        if (take > size - done)
            take = size - done;
        memcpy(out + done, sector + skip, take);
        done += take;
        offset += take;
    }
    *got = done;
    return HDDVD_OK;
}
