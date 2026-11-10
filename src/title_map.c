#include "internal.h"
#include <stdlib.h>
#include <string.h>

struct title_clip
{
    size_t clip;
    uint64_t begin;
    uint64_t end;
    uint64_t clip_begin;
    hddvd_tmap *tmap;
    hddvd_vti_evob evob;
    char *evo_uri;
};

struct hddvd_title_map
{
    struct title_clip *clips;
    size_t count;
};

int hddvd_xpl_time_to_ticks(hddvd_xpl_time time, hddvd_time *out)
{
    if (!out)
        return HDDVD_EINVAL;
    /* 90000 * 1001 / 60000 = 3003/2 ticks per frame. */
    if (time.frame_rate != 60)
        return HDDVD_ENOTSUP;
    if (time.frames > UINT64_MAX / 3003u)
        return HDDVD_ELIMIT;
    out->ticks = time.frames * 3003u / 2u;
    return HDDVD_OK;
}

/* The directory part of a URI, up to and including its last '/', followed
   by name. */
static char *sibling_uri(const char *uri, const char *name)
{
    const char *slash = strrchr(uri, '/');
    size_t prefix = slash ? (size_t)(slash - uri) + 1 : 0, length = strlen(name);
    char *out = malloc(prefix + length + 1);
    if (!out)
        return NULL;
    memcpy(out, uri, prefix);
    memcpy(out + prefix, name, length + 1);
    return out;
}

static int open_vti(hddvd_ctx *ctx, const char *uri, hddvd_vti **vti)
{
    hddvd_stream *stream = NULL;
    int rc = hddvd_open_resource(ctx, uri, &stream);
    if (rc)
        return rc;
    rc = hddvd_vti_open(stream, vti);
    hddvd_stream_close(stream);
    if (rc)
        hddvd_ctx_set_error(ctx, rc, HDDVD_SUBSYSTEM_TIME_MAP, uri, "invalid VTI file");
    return rc;
}

static int clip_times(const hddvd_xpl_clip *value, struct title_clip *clip)
{
    hddvd_time begin = {0}, end, clip_begin = {0};
    int rc = HDDVD_OK;
    if (!value->has_title_end)
        return HDDVD_EPARSE;
    if (value->has_title_begin)
        rc = hddvd_xpl_time_to_ticks(value->title_begin, &begin);
    if (!rc)
        rc = hddvd_xpl_time_to_ticks(value->title_end, &end);
    if (!rc && value->has_clip_begin)
        rc = hddvd_xpl_time_to_ticks(value->clip_begin, &clip_begin);
    if (rc)
        return rc;
    if (end.ticks <= begin.ticks)
        return HDDVD_EPARSE;
    clip->begin = begin.ticks;
    clip->end = end.ticks;
    clip->clip_begin = clip_begin.ticks;
    return HDDVD_OK;
}

/* Opens one clip's time map, finds its EVOB in the VTI (reusing the open
   VTI when the map names the same one), and checks the clip fits. */
static int open_clip(hddvd_ctx *ctx, const char *src, struct title_clip *clip, hddvd_vti **vti,
                     char **vti_uri)
{
    hddvd_stream *stream = NULL;
    hddvd_map_header header;
    char *uri;
    size_t index;
    uint64_t duration;
    int rc = hddvd_open_resource(ctx, src, &stream);
    if (rc)
        return rc;
    rc = hddvd_tmap_open(stream, &clip->tmap);
    hddvd_stream_close(stream);
    if (rc)
    {
        hddvd_ctx_set_error(ctx, rc, HDDVD_SUBSYSTEM_TIME_MAP, src, "invalid time map");
        return rc;
    }
    hddvd_tmap_get_header(clip->tmap, &header);
    uri = sibling_uri(src, header.vti_name);
    if (!uri)
        return HDDVD_ENOMEM;
    if (!*vti_uri || strcmp(*vti_uri, uri))
    {
        hddvd_vti_close(*vti);
        *vti = NULL;
        free(*vti_uri);
        *vti_uri = uri;
        rc = open_vti(ctx, uri, vti);
        if (rc)
            return rc;
    }
    else
        free(uri);
    if (hddvd_vti_find_evob(*vti, hddvd_tmap_evob_number(clip->tmap), &index))
    {
        hddvd_ctx_set_error(ctx, HDDVD_EPARSE, HDDVD_SUBSYSTEM_TIME_MAP, src,
                            "time map names an EVOB its VTI does not list");
        return HDDVD_EPARSE;
    }
    hddvd_vti_get_evob(*vti, index, &clip->evob);
    duration = hddvd_tmap_duration(clip->tmap).ticks;
    if (clip->clip_begin > duration || clip->end - clip->begin > duration - clip->clip_begin)
    {
        hddvd_ctx_set_error(ctx, HDDVD_EPARSE, HDDVD_SUBSYSTEM_TIME_MAP, src,
                            "clip is longer than its time map");
        return HDDVD_EPARSE;
    }
    clip->evo_uri = sibling_uri(src, clip->evob.file_name);
    return clip->evo_uri ? HDDVD_OK : HDDVD_ENOMEM;
}

int hddvd_title_map_open(hddvd_ctx *ctx, const hddvd_xpl *xpl, size_t title, hddvd_title_map **out)
{
    hddvd_title_map *map;
    hddvd_vti *vti = NULL;
    char *vti_uri = NULL;
    size_t i, count;
    int rc = HDDVD_OK;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!ctx || !xpl || title >= hddvd_xpl_title_count(xpl))
    {
        hddvd_ctx_set_error(ctx, HDDVD_EINVAL, HDDVD_SUBSYSTEM_TIME_MAP, NULL,
                            "invalid title map request");
        return HDDVD_EINVAL;
    }
    count = hddvd_xpl_clip_count(xpl, title);
    map = calloc(1, sizeof(*map));
    if (!map || (count && !(map->clips = calloc(count, sizeof(*map->clips)))))
    {
        free(map);
        return HDDVD_ENOMEM;
    }
    for (i = 0; i < count && !rc; i++)
    {
        hddvd_xpl_clip value;
        hddvd_xpl_clip_kind kind;
        struct title_clip *clip = &map->clips[map->count];
        hddvd_xpl_get_clip_kind(xpl, title, i, &kind);
        if (kind != HDDVD_XPL_CLIP_PRIMARY_AUDIO_VIDEO)
            continue;
        hddvd_xpl_get_clip(xpl, title, i, &value);
        clip->clip = i;
        rc = value.src ? clip_times(&value, clip) : HDDVD_EPARSE;
        if (rc)
            hddvd_ctx_set_error(ctx, rc, HDDVD_SUBSYSTEM_TIME_MAP, value.src,
                                "clip has no usable source or title times");
        else
            rc = open_clip(ctx, value.src, clip, &vti, &vti_uri);
        /* Count the clip so close releases whatever it opened. */
        map->count++;
    }
    hddvd_vti_close(vti);
    free(vti_uri);
    if (rc)
    {
        hddvd_title_map_close(map);
        return rc;
    }
    hddvd_clear_error(ctx);
    *out = map;
    return HDDVD_OK;
}

void hddvd_title_map_close(hddvd_title_map *map)
{
    size_t i;
    if (!map)
        return;
    for (i = 0; i < map->count; i++)
    {
        hddvd_tmap_close(map->clips[i].tmap);
        free(map->clips[i].evo_uri);
    }
    free(map->clips);
    free(map);
}

size_t hddvd_title_map_clip_count_internal(const hddvd_title_map *map)
{
    return map->count;
}

void hddvd_title_map_get_clip_internal(const hddvd_title_map *map, size_t index,
                                       hddvd_title_map_clip *out)
{
    const struct title_clip *clip = &map->clips[index];
    out->begin = clip->begin;
    out->end = clip->end;
    out->clip_begin = clip->clip_begin;
    out->tmap = clip->tmap;
    out->evo_uri = clip->evo_uri;
}

int hddvd_title_map_locate(const hddvd_title_map *map, hddvd_time time, hddvd_title_position *out)
{
    size_t i;
    if (!map || !out)
        return HDDVD_EINVAL;
    for (i = 0; i < map->count; i++)
    {
        const struct title_clip *clip = &map->clips[i];
        hddvd_time offset;
        hddvd_tmap_entry entry;
        size_t index;
        if (time.ticks < clip->begin || time.ticks >= clip->end)
            continue;
        offset.ticks = clip->clip_begin + (time.ticks - clip->begin);
        if (hddvd_tmap_find(clip->tmap, offset, &index))
            return HDDVD_ENOTFOUND;
        hddvd_tmap_get_entry(clip->tmap, index, &entry);
        memset(out, 0, sizeof(*out));
        out->clip = clip->clip;
        out->evo_uri = clip->evo_uri;
        out->evob_number = clip->evob.number;
        out->evob_offset = offset;
        out->pts.ticks = clip->evob.start.ticks + offset.ticks;
        out->entry = index;
        out->sector = entry.first_sector;
        out->entry_pts.ticks = clip->evob.start.ticks + entry.start.ticks;
        return HDDVD_OK;
    }
    return HDDVD_ENOTFOUND;
}
