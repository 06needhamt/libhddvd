#ifndef LIBHDDVD_SCRIPT_ENGINE_H
#define LIBHDDVD_SCRIPT_ENGINE_H
#include "hddvd.h"

/* The boundary between runtime.c, which owns the public API, argument checks
   and source decoding, and one ECMAScript engine backend chosen at build time
   (script_quickjs.c, or script_none.c when there is no engine). Nothing outside
   runtime.c includes this header, so no engine type reaches the public API. */
typedef struct hddvd_engine hddvd_engine;

/* What an evaluate or call ended with. message and trace are allocated with
   malloc and owned by the caller; either may be NULL. */
typedef struct hddvd_engine_report
{
    int status;
    hddvd_runtime_limit limit;
    char *message;
    char *trace;
} hddvd_engine_report;

/* Name and version of the engine, or NULL in builds without one. */
const char *hddvd_engine_name(void);
/* Limits are already validated. HDDVD_ELIMIT when the heap budget cannot hold
   an empty context. */
int hddvd_engine_create(const hddvd_runtime_limits *, hddvd_engine **);
void hddvd_engine_destroy(hddvd_engine *);
/* source is valid UTF-8 of `size` bytes followed by a NUL byte. */
int hddvd_engine_evaluate(hddvd_engine *, const char *name, const char *source, size_t size,
                          hddvd_engine_report *);
int hddvd_engine_call(hddvd_engine *, const char *function, hddvd_engine_report *);
#endif
