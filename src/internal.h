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
#endif
