#ifndef LIBHDDVD_INTERNAL_H
#define LIBHDDVD_INTERNAL_H
#include "hddvd.h"
#include <stdio.h>
struct hddvd_ctx { char *root; hddvd_log_fn log_fn; void *log_opaque; };
struct hddvd_stream {
    uint64_t size;
    unsigned refs;
    int (*read_at)(struct hddvd_stream*,uint64_t,void*,size_t,size_t*);
    void (*destroy)(struct hddvd_stream*);
};
char *hddvd_strdup(const char *s);
int hddvd_path_is_dir(const char *p);
int hddvd_path_exists(const char *p);
char *hddvd_path_join(const char *a,const char *b);
int hddvd_ascii_endswith_ci(const char *s,const char *suffix);
int hddvd_ascii_equal_ci(const char *a,const char *b);
int hddvd_stream_slice(hddvd_stream*,uint64_t,uint64_t,hddvd_stream**);
void hddvd_stream_retain(hddvd_stream*);
#endif
