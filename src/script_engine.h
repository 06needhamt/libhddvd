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

/* Host objects. The runtime's prelude (hdi_prelude.js) is a script whose
   value is a function; install calls it with an object of native functions,
   one per name, and the global object, and keeps what it returns (an object
   of entry functions) out of the scripts' reach for invoke. */
typedef enum hddvd_engine_type
{
    HDDVD_ENGINE_UNDEFINED,
    HDDVD_ENGINE_NULL,
    HDDVD_ENGINE_BOOLEAN,
    HDDVD_ENGINE_NUMBER,
    HDDVD_ENGINE_STRING,
    /* Objects and functions; no payload. */
    HDDVD_ENGINE_OTHER
} hddvd_engine_type;
/* A string is UTF-8 of `length` bytes and may contain NUL bytes; booleans
   are numbers 0 and 1. */
typedef struct hddvd_engine_value
{
    hddvd_engine_type type;
    double number;
    const char *string;
    size_t length;
} hddvd_engine_value;
/* A native sets *result (undefined when left alone) and returns HDDVD_OK,
   or returns an error and sets *message, which the engine throws as an
   Error. Strings in *result and *message must stay valid until the native
   is called again. */
typedef int (*hddvd_engine_native_fn)(void *opaque, unsigned index, const hddvd_engine_value *args,
                                      size_t count, hddvd_engine_value *result,
                                      const char **message);
int hddvd_engine_install(hddvd_engine *, const char *source, size_t size, const char *const *names,
                         size_t count, hddvd_engine_native_fn, void *opaque, hddvd_engine_report *);
/* Calls the entry function `entry`. A number or boolean it returns is stored
   in *result when result is not NULL (NaN for anything else). */
int hddvd_engine_invoke(hddvd_engine *, const char *entry, const hddvd_engine_value *args,
                        size_t count, double *result, hddvd_engine_report *);
#endif
