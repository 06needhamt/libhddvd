#ifndef LIBHDDVD_INTERNAL_H
#define LIBHDDVD_INTERNAL_H
#include "hddvd.h"
#include <stdio.h>
#include <sys/stat.h>

#ifdef _WIN32
#define hddvd_stat _stat64
#define HDDVD_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#define HDDVD_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#else
#define hddvd_stat stat
#define HDDVD_ISDIR(mode) S_ISDIR(mode)
#define HDDVD_ISREG(mode) S_ISREG(mode)
#endif

typedef struct hddvd_directory hddvd_directory;
hddvd_directory *hddvd_directory_open(const char *path);
const char *hddvd_directory_read(hddvd_directory *directory);
void hddvd_directory_close(hddvd_directory *directory);
struct hddvd_ctx
{
    char *root;
    hddvd_log_fn log_fn;
    void *log_opaque;
    hddvd_error_detail error;
    hddvd_config_limits limits;
};
struct hddvd_stream
{
    uint64_t size;
    unsigned refs;
    int (*read_at)(struct hddvd_stream *, uint64_t, void *, size_t, size_t *);
    void (*destroy)(struct hddvd_stream *);
};
char *hddvd_strdup(const char *s);
int hddvd_path_is_dir(const char *p);
int hddvd_path_exists(const char *p);
char *hddvd_path_join(const char *a, const char *b);
int hddvd_ascii_endswith_ci(const char *s, const char *suffix);
int hddvd_ascii_equal_ci(const char *a, const char *b);
int hddvd_stream_slice(hddvd_stream *, uint64_t, uint64_t, hddvd_stream **);
void hddvd_stream_retain(hddvd_stream *);
void hddvd_ctx_set_error(const hddvd_ctx *, int, hddvd_subsystem, const char *, const char *);
/* Resolves a disc-relative path ("HVDVD_TS/HV001T01.EVO") component by
   component, case-insensitively, as hddvd_open_resource does. */
int hddvd_resolve_disc_path(const hddvd_ctx *, const char *, char **);
/* A title map's primary clips, for titles.c: title times and the offset into
   the clip's EVOB in 90 kHz ticks, and its time map and EVO URI, both
   borrowed from the map. */
typedef struct hddvd_title_map_clip
{
    uint64_t begin;
    uint64_t end;
    uint64_t clip_begin;
    const hddvd_tmap *tmap;
    const char *evo_uri;
} hddvd_title_map_clip;
size_t hddvd_title_map_clip_count_internal(const hddvd_title_map *);
void hddvd_title_map_get_clip_internal(const hddvd_title_map *, size_t, hddvd_title_map_clip *);
/* titles.c, for std_nav.c: the extent of a Standard Content title that is
   cell `cell` (0-based) of its title set's PGC `pgc` (1-based);
   HDDVD_ENOTFOUND when the title does not play it. */
int hddvd_titles_find_cell_internal(const hddvd_titles *, size_t title, uint16_t pgc, size_t cell,
                                    size_t *extent);
/* The reverse: the PGC and cell of a Standard Content title that hold the
   byte at `offset` of its stream. */
int hddvd_titles_cell_at_internal(const hddvd_titles *, size_t title, uint64_t offset,
                                  uint16_t *pgc, size_t *cell);
int hddvd_config_limits_valid(const hddvd_config_limits *);
/* Strips a recognized envelope whose type is `expected` or clear text.
   Non-envelope input passes through unchanged with *owned set to NULL;
   otherwise *owned holds the payload and must be freed by the caller. */
int hddvd_object_unwrap(const void *, size_t, hddvd_object_type, unsigned char **, const void **,
                        size_t *);
/* The decoration array parallel to hddvd_scene_nodes, for display.c. */
const hddvd_scene_decoration *hddvd_scene_decorations_internal(const hddvd_scene *);
/* Stream attribute records shared by IFO title sets and VTIs (ifo.c). The
   language is the two bytes at language_at when they are lowercase letters. */
void hddvd_decode_video_attributes(const unsigned char *, hddvd_video_attributes *);
void hddvd_decode_stream_attributes(const unsigned char *, size_t, size_t,
                                    hddvd_stream_attributes *);
/* Script style overrides (runtime_dom.c), on a styles object the runtime owns.
   override parses one property into a node's computed value: HDDVD_ENOTSUP
   for a property or keyword the model does not know, HDDVD_EPARSE for a
   malformed value, and in both cases the node is unchanged. restore resets a
   node's computed value from `pristine`, opened from the same markup.
   declared returns a property's text as written in markup, on the node or
   the style it takes the value from, or NULL. */
int hddvd_style_override(hddvd_xmu_styles *, size_t, const char *, const char *);
int hddvd_style_restore(hddvd_xmu_styles *, const hddvd_xmu_styles *, size_t);
const char *hddvd_style_declared(const hddvd_xmu_styles *, size_t, const char *);
#endif
