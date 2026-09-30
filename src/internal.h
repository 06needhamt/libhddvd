#ifndef LIBHDDVD_INTERNAL_H
#define LIBHDDVD_INTERNAL_H
#include "hddvd.h"
#include <stdio.h>
struct hddvd_ctx { char *root; hddvd_log_fn log_fn; void *log_opaque; };
char *hddvd_strdup(const char *s);
int hddvd_path_is_dir(const char *p);
int hddvd_path_exists(const char *p);
char *hddvd_path_join(const char *a,const char *b);
int hddvd_ascii_endswith_ci(const char *s,const char *suffix);
#endif
