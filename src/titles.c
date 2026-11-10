#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Playable titles (docs/formats/ifo-map.md). Standard Content: the VMG's
   title table joined to each title set's chapters, PGCs and cells, and the
   cells' sectors in the title set's EVO. Advanced Content: a playlist's
   titles, whose primary clips name EVOBUs of an EVO through their time
   maps. */
#define SECTOR 2048u
#define MAX_TITLE_SET 999u

/* An EVO file extents read from. Only one is open at a time, so a playlist
   with hundreds of clips does not hold hundreds of files open. */
typedef struct title_file
{
    char *uri;
    char *path;
    uint64_t sectors;
} title_file;

typedef struct title_entry
{
    hddvd_title_info info;
    hddvd_title_extent *extents;
    /* The title_file each extent reads from. */
    size_t *files;
    size_t capacity;
    hddvd_title_chapter *chapters;
    /* Standard Content: the PGCs played (1-based numbers), in order, and
       each one's first extent. */
    uint16_t *pgcs;
    size_t *pgc_firsts;
    size_t pgc_count;
} title_entry;

typedef struct title_set
{
    uint16_t number;
    size_t file;
} title_set;

struct hddvd_titles
{
    hddvd_ctx *ctx;
    title_entry *titles;
    size_t count;
    title_file *files;
    size_t file_count;
    size_t file_capacity;
    hddvd_stream *open;
    size_t open_file;
};

void hddvd_titles_close(hddvd_titles *titles)
{
    size_t i;
    if (!titles)
        return;
    for (i = 0; i < titles->count; i++)
    {
        free(titles->titles[i].extents);
        free(titles->titles[i].files);
        free(titles->titles[i].chapters);
        free(titles->titles[i].pgcs);
        free(titles->titles[i].pgc_firsts);
    }
    for (i = 0; i < titles->file_count; i++)
    {
        free(titles->files[i].uri);
        free(titles->files[i].path);
    }
    hddvd_stream_close(titles->open);
    free(titles->titles);
    free(titles->files);
    free(titles);
}

static int fail(const hddvd_ctx *ctx, int rc, const char *uri, const char *message)
{
    hddvd_ctx_set_error(ctx, rc, HDDVD_SUBSYSTEM_DISC, uri, message);
    return rc;
}

/* Finds or adds the EVO a file:///dvddisc/ URI names. The file must be on
   the disc; it is opened only to learn its size. */
static int find_file(hddvd_titles *titles, const char *uri, size_t *out)
{
    hddvd_uri *parsed = NULL;
    hddvd_stream *stream = NULL;
    title_file *file;
    size_t i;
    int rc;
    for (i = 0; i < titles->file_count; i++)
        if (!strcmp(titles->files[i].uri, uri))
        {
            *out = i;
            return HDDVD_OK;
        }
    if (titles->file_count == titles->file_capacity)
    {
        size_t grown = titles->file_capacity ? titles->file_capacity * 2 : 8;
        title_file *more = realloc(titles->files, grown * sizeof(*more));
        if (!more)
            return HDDVD_ENOMEM;
        titles->files = more;
        titles->file_capacity = grown;
    }
    file = &titles->files[titles->file_count];
    memset(file, 0, sizeof(*file));
    rc = hddvd_uri_parse(uri, &parsed);
    if (!rc && hddvd_uri_archive_name(parsed))
        rc = HDDVD_EPARSE;
    if (!rc)
        rc = hddvd_resolve_disc_path(titles->ctx, hddvd_uri_disc_path(parsed), &file->path);
    hddvd_uri_free(parsed);
    if (!rc)
        rc = hddvd_stream_open_file(file->path, &stream);
    if (!rc && !(file->uri = hddvd_strdup(uri)))
        rc = HDDVD_ENOMEM;
    if (rc)
    {
        hddvd_stream_close(stream);
        free(file->path);
        return rc == HDDVD_ENOMEM ? rc : fail(titles->ctx, rc, uri, "EVO not found");
    }
    file->sectors = hddvd_stream_size(stream) / SECTOR;
    hddvd_stream_close(stream);
    *out = titles->file_count++;
    return HDDVD_OK;
}

/* Appends an extent of `count` sectors from `first` in `file`, playing for
   `duration` from title time `start`. */
static int add_extent(hddvd_titles *titles, title_entry *t, size_t file, uint64_t first,
                      uint64_t count, hddvd_time start, hddvd_time duration)
{
    const title_file *f = &titles->files[file];
    hddvd_title_extent *e;
    if (!count || first > UINT32_MAX || count > UINT32_MAX || first + count > f->sectors)
        return fail(titles->ctx, HDDVD_EPARSE, f->uri, "title data outside the EVO");
    if (t->info.extent_count == t->capacity)
    {
        size_t grown = t->capacity ? t->capacity * 2 : 8;
        hddvd_title_extent *more = realloc(t->extents, grown * sizeof(*more));
        size_t *files;
        if (!more)
            return HDDVD_ENOMEM;
        memset(more + t->capacity, 0, (grown - t->capacity) * sizeof(*more));
        t->extents = more;
        files = realloc(t->files, grown * sizeof(*files));
        if (!files)
            return HDDVD_ENOMEM;
        memset(files + t->capacity, 0, (grown - t->capacity) * sizeof(*files));
        t->files = files;
        t->capacity = grown;
    }
    t->files[t->info.extent_count] = file;
    e = &t->extents[t->info.extent_count++];
    e->evo_uri = f->uri;
    e->first_sector = (uint32_t)first;
    e->sector_count = (uint32_t)count;
    e->offset = t->info.size;
    e->start = start;
    e->duration = duration;
    t->info.size += count * SECTOR;
    return HDDVD_OK;
}

/* Opens title set `number`: its IFO, and the EVO its cells must fit in. */
static int open_set(hddvd_titles *titles, uint16_t number, hddvd_vts **vts, title_set *set)
{
    char uri[64];
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
    snprintf(uri, sizeof(uri), "file:///dvddisc/HVDVD_TS/HV%03uT01.EVO", (unsigned)number);
    set->number = number;
    return find_file(titles, uri, &set->file);
}

/* Adds pgc's cells to the title; *first is the index of its first extent. */
static int add_pgc(hddvd_titles *titles, title_entry *t, const hddvd_vts *vts, size_t pgc,
                   const title_set *set, size_t *first)
{
    const char *evo_uri = titles->files[set->file].uri;
    hddvd_vts_pgc info;
    size_t i;
    if (hddvd_vts_get_pgc(vts, pgc, &info))
        return fail(titles->ctx, HDDVD_EPARSE, evo_uri, "chapter names a missing PGC");
    *first = t->info.extent_count;
    for (i = 0; i < info.cell_count; i++)
    {
        hddvd_vts_cell cell;
        hddvd_time duration;
        int rc;
        if (hddvd_vts_get_cell(vts, pgc, i, &cell) ||
            hddvd_vts_time_to_ticks(&cell.playback_time, &duration))
            return fail(titles->ctx, HDDVD_EPARSE, evo_uri, "invalid cell");
        if (cell.last_sector < cell.first_vobu_sector)
            return fail(titles->ctx, HDDVD_EPARSE, evo_uri, "cell outside the EVO");
        rc = add_extent(titles, t, set->file, cell.first_vobu_sector,
                        (uint64_t)cell.last_sector - cell.first_vobu_sector + 1, t->info.duration,
                        duration);
        if (rc)
            return rc;
        t->info.duration.ticks += duration.ticks;
    }
    return HDDVD_OK;
}

/* Builds a title from its chapters: the PGCs they name, in the order they
   first appear. */
static int build_title(hddvd_titles *titles, title_entry *t, const hddvd_vts *vts, size_t title,
                       const title_set *set)
{
    const char *evo_uri = titles->files[set->file].uri;
    size_t count = hddvd_vts_chapter_count(vts, title), i, j;
    uint16_t *pgcs;
    size_t *firsts, pgc_count = 0;
    int rc = HDDVD_OK;
    if (!count)
        return fail(titles->ctx, HDDVD_EPARSE, evo_uri, "title has no chapters");
    pgcs = t->pgcs = calloc(count, sizeof(*pgcs));
    firsts = t->pgc_firsts = calloc(count, sizeof(*firsts));
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
            rc = fail(titles->ctx, HDDVD_EPARSE, evo_uri, "invalid chapter");
            break;
        }
        for (j = 0; j < pgc_count && pgcs[j] != chapter.pgc; j++)
            ;
        if (j == pgc_count)
        {
            pgcs[pgc_count] = chapter.pgc;
            rc = add_pgc(titles, t, vts, (size_t)chapter.pgc - 1, set, &firsts[pgc_count]);
            pgc_count++;
            if (rc)
                break;
        }
        extent = firsts[j] + cell - 1u;
        if (extent >= t->info.extent_count)
        {
            rc = fail(titles->ctx, HDDVD_EPARSE, evo_uri, "chapter starts past its PGC");
            break;
        }
        t->chapters[i].start = t->extents[extent].start;
        t->chapters[i].offset = t->extents[extent].offset;
    }
    if (!rc)
    {
        t->info.chapter_count = count;
        t->pgc_count = pgc_count;
    }
    return rc;
}

int hddvd_titles_find_cell_internal(const hddvd_titles *titles, size_t title, uint16_t pgc,
                                    size_t cell, size_t *extent)
{
    const title_entry *t = &titles->titles[title];
    size_t i;
    for (i = 0; i < t->pgc_count; i++)
        if (t->pgcs[i] == pgc)
        {
            size_t end = i + 1 < t->pgc_count ? t->pgc_firsts[i + 1] : t->info.extent_count;
            if (cell >= end - t->pgc_firsts[i])
                return HDDVD_ENOTFOUND;
            *extent = t->pgc_firsts[i] + cell;
            return HDDVD_OK;
        }
    return HDDVD_ENOTFOUND;
}

int hddvd_titles_open(hddvd_ctx *ctx, hddvd_titles **out)
{
    static const char vmg_uri[] = "file:///dvddisc/HVDVD_TS/HV000I01.IFO";
    hddvd_titles *titles;
    hddvd_stream *stream = NULL;
    hddvd_vmg *vmg = NULL;
    hddvd_vts **vts = NULL;
    title_set *sets = NULL;
    size_t i, count, set_count = 0;
    int rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!ctx)
        return HDDVD_EINVAL;
    rc = hddvd_open_resource(ctx, vmg_uri, &stream);
    if (rc == HDDVD_ENOTFOUND)
        return fail(ctx, rc, vmg_uri,
                    "no Standard Content title table (Advanced Content titles come from the "
                    "playlist)");
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
        sets = calloc(count, sizeof(*sets));
        vts = calloc(count, sizeof(*vts));
    }
    if (!titles || !titles->titles || !sets || !vts)
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
        for (s = 0; s < set_count && sets[s].number != title.title_set; s++)
            ;
        if (s == set_count)
        {
            set_count++;
            rc = open_set(titles, title.title_set, &vts[s], &sets[s]);
            if (rc)
                break;
        }
        if (title.title_set_title > hddvd_vts_title_count(vts[s]))
        {
            rc = fail(ctx, HDDVD_EPARSE, titles->files[sets[s].file].uri,
                      "title not in its title set");
            break;
        }
        t->info.title_set = title.title_set;
        t->info.title_set_title = title.title_set_title;
        t->info.angle_count = title.angle_count;
        rc = build_title(titles, t, vts[s], (size_t)title.title_set_title - 1, &sets[s]);
    }
    if (vts)
        for (i = 0; i < count; i++)
            hddvd_vts_close(vts[i]);
    free(vts);
    free(sets);
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

/* The extent of a playlist title that plays at `ticks`, or the next one
   when no clip covers it; extent_count when none follows. */
static size_t extent_at(const title_entry *t, uint64_t ticks)
{
    size_t i;
    for (i = 0; i < t->info.extent_count; i++)
        if (ticks < t->extents[i].start.ticks + t->extents[i].duration.ticks)
            break;
    return i;
}

/* Builds a title from a playlist title's primary clips, in title-time
   order. A clip's extent runs from the EVOBU that holds its first frame to
   the end of the one that holds its last. */
static int build_playlist_title(hddvd_titles *titles, title_entry *t, const hddvd_xpl *xpl,
                                size_t title)
{
    hddvd_title_map *map = NULL;
    hddvd_title_map_clip *clips = NULL;
    hddvd_xpl_title_info info;
    const char *name;
    size_t count, i, j;
    int rc = hddvd_title_map_open(titles->ctx, xpl, title, &map);
    if (rc)
        return rc;
    hddvd_xpl_get_title(xpl, title, &info);
    name = info.id ? info.id : "playlist title";
    count = hddvd_title_map_clip_count_internal(map);
    if (count && !(clips = malloc(count * sizeof(*clips))))
        rc = HDDVD_ENOMEM;
    for (i = 0; !rc && i < count; i++)
    {
        hddvd_title_map_clip clip;
        hddvd_title_map_get_clip_internal(map, i, &clip);
        for (j = i; j > 0 && clips[j - 1].begin > clip.begin; j--)
            clips[j] = clips[j - 1];
        clips[j] = clip;
    }
    for (i = 0; !rc && i < count; i++)
    {
        const hddvd_title_map_clip *clip = &clips[i];
        hddvd_tmap_entry first, last;
        hddvd_time start, duration, at;
        size_t file, a, b;
        if (i && clip->begin < clips[i - 1].end)
        {
            rc = fail(titles->ctx, HDDVD_EPARSE, name, "primary clips overlap");
            break;
        }
        start.ticks = clip->begin;
        duration.ticks = clip->end - clip->begin;
        at.ticks = clip->clip_begin;
        rc = hddvd_tmap_find(clip->tmap, at, &a);
        at.ticks += duration.ticks - 1;
        if (!rc)
            rc = hddvd_tmap_find(clip->tmap, at, &b);
        if (rc || hddvd_tmap_get_entry(clip->tmap, a, &first) ||
            hddvd_tmap_get_entry(clip->tmap, b, &last) || last.first_sector < first.first_sector)
        {
            rc = fail(titles->ctx, HDDVD_EPARSE, clip->evo_uri, "clip outside its time map");
            break;
        }
        rc = find_file(titles, clip->evo_uri, &file);
        if (!rc)
            rc = add_extent(titles, t, file, first.first_sector,
                            (uint64_t)last.first_sector + last.sector_count - first.first_sector,
                            start, duration);
        if (!rc)
            t->info.duration.ticks = clip->end;
    }
    if (!rc && info.has_duration)
    {
        hddvd_time declared;
        rc = hddvd_xpl_time_to_ticks(info.duration, &declared);
        if (rc)
            fail(titles->ctx, rc, name, "unsupported title duration");
        else if (declared.ticks > t->info.duration.ticks)
            t->info.duration = declared;
    }
    count = hddvd_xpl_chapter_count(xpl, title);
    if (!rc && count && !(t->chapters = calloc(count, sizeof(*t->chapters))))
        rc = HDDVD_ENOMEM;
    for (i = 0; !rc && i < count; i++)
    {
        hddvd_xpl_chapter chapter;
        hddvd_time begin;
        size_t extent;
        hddvd_xpl_get_chapter(xpl, title, i, &chapter);
        rc = hddvd_xpl_time_to_ticks(chapter.begin, &begin);
        if (rc)
        {
            fail(titles->ctx, rc, name, "unsupported chapter time");
            break;
        }
        t->chapters[i].start = begin;
        extent = extent_at(t, begin.ticks);
        if (extent == t->info.extent_count)
            t->chapters[i].offset = t->info.size;
        else
        {
            const hddvd_title_extent *e = &t->extents[extent];
            hddvd_title_position position;
            t->chapters[i].offset = e->offset;
            /* Inside a clip: the EVOBU that plays the chapter's first frame. */
            if (begin.ticks >= e->start.ticks &&
                hddvd_title_map_locate(map, begin, &position) == HDDVD_OK &&
                position.sector >= e->first_sector)
                t->chapters[i].offset += (uint64_t)(position.sector - e->first_sector) * SECTOR;
        }
    }
    if (!rc)
        t->info.chapter_count = count;
    free(clips);
    hddvd_title_map_close(map);
    return rc;
}

int hddvd_titles_open_playlist(hddvd_ctx *ctx, const hddvd_xpl *xpl, hddvd_titles **out)
{
    hddvd_titles *titles;
    size_t i, count;
    int rc = HDDVD_OK;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!ctx || !xpl)
        return HDDVD_EINVAL;
    count = hddvd_xpl_title_count(xpl);
    titles = calloc(1, sizeof(*titles));
    if (!titles || (count && !(titles->titles = calloc(count, sizeof(*titles->titles)))))
    {
        free(titles);
        return HDDVD_ENOMEM;
    }
    titles->ctx = ctx;
    titles->count = count;
    for (i = 0; !rc && i < count; i++)
        rc = build_playlist_title(titles, &titles->titles[i], xpl, i);
    if (rc)
    {
        hddvd_titles_close(titles);
        return rc;
    }
    hddvd_clear_error(ctx);
    *out = titles;
    return HDDVD_OK;
}

/* The number in a playlist file name, VPLSTnnn.XPL in any case, or -1. */
static int playlist_number(const char *name)
{
    if (strlen(name) != 12 || !hddvd_ascii_endswith_ci(name, ".XPL") ||
        (name[0] != 'V' && name[0] != 'v') || (name[1] != 'P' && name[1] != 'p') ||
        (name[2] != 'L' && name[2] != 'l') || (name[3] != 'S' && name[3] != 's') ||
        (name[4] != 'T' && name[4] != 't') || name[5] < '0' || name[5] > '9' || name[6] < '0' ||
        name[6] > '9' || name[7] < '0' || name[7] > '9')
        return -1;
    return (name[5] - '0') * 100 + (name[6] - '0') * 10 + (name[7] - '0');
}

int hddvd_open_playlist(hddvd_ctx *ctx, hddvd_xpl **out)
{
    static const char directory_uri[] = "file:///dvddisc/ADV_OBJ";
    hddvd_directory *directory;
    hddvd_stream *stream = NULL;
    char uri[64], *path = NULL;
    const char *name;
    unsigned char *bytes;
    uint64_t size;
    size_t got = 0;
    int best = -1, rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!ctx)
        return HDDVD_EINVAL;
    rc = hddvd_resolve_disc_path(ctx, "ADV_OBJ", &path);
    directory = rc ? NULL : hddvd_directory_open(path);
    free(path);
    while (directory && (name = hddvd_directory_read(directory)))
        if (playlist_number(name) > best)
            best = playlist_number(name);
    if (directory)
        hddvd_directory_close(directory);
    if (best < 0)
        return fail(ctx, HDDVD_ENOTFOUND, directory_uri, "no playlist (ADV_OBJ/VPLSTnnn.XPL)");
    snprintf(uri, sizeof(uri), "%s/VPLST%03d.XPL", directory_uri, best);
    rc = hddvd_open_resource(ctx, uri, &stream);
    if (rc)
        return rc;
    size = hddvd_stream_size(stream);
    bytes = size && size <= SIZE_MAX ? malloc((size_t)size) : NULL;
    if (!bytes)
        rc = size ? HDDVD_ENOMEM : HDDVD_EPARSE;
    else
        rc = hddvd_stream_read_at(stream, 0, bytes, (size_t)size, &got);
    hddvd_stream_close(stream);
    if (!rc && got != size)
        rc = HDDVD_EIO;
    if (!rc)
        rc = hddvd_xpl_open_with_config(bytes, (size_t)size, &ctx->limits, out);
    free(bytes);
    if (rc)
        return fail(ctx, rc, uri, "invalid playlist");
    hddvd_clear_error(ctx);
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
    if (!t->info.chapter_count)
        return HDDVD_ENOTFOUND;
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
        size_t file;
        int rc;
        if (within >= (uint64_t)e->sector_count * SECTOR)
        {
            lo++;
            continue;
        }
        file = t->files[lo];
        if (!titles->open || titles->open_file != file)
        {
            hddvd_stream_close(titles->open);
            titles->open = NULL;
            rc = hddvd_stream_open_file(titles->files[file].path, &titles->open);
            if (rc)
            {
                *got = done;
                return fail(titles->ctx, rc, e->evo_uri, "EVO cannot be opened");
            }
            titles->open_file = file;
        }
        at = ((uint64_t)e->first_sector + within / SECTOR) * SECTOR;
        rc = hddvd_stream_read_at(titles->open, at, sector, SECTOR, &n);
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
