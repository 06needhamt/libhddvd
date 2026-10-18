#ifndef LIBHDDVD_RUNTIME_INTERNAL_H
#define LIBHDDVD_RUNTIME_INTERNAL_H
#include "internal.h"
#include "script_engine.h"

/* Shared by runtime.c (engine, sources, diagnostics) and runtime_dom.c (the
   host objects' document state and natives). */
#define HDDVD_RUNTIME_STATE_COUNT 5
#define HDDVD_RUNTIME_MAX_ISSUES 256

struct hddvd_runtime_style
{
    char *name;
    char *value;
};

/* Script values for one markup node; NULL state slots hold no script value. */
struct hddvd_runtime_node
{
    char *state[HDDVD_RUNTIME_STATE_COUNT];
    struct hddvd_runtime_style *styles;
    size_t style_count;
};

struct hddvd_runtime
{
    hddvd_engine *engine;
    hddvd_runtime_limits limits;
    hddvd_engine_report report;
    /* The document: borrowed markup, styles with script values applied, and
       an untouched copy to restore from. */
    const hddvd_xmu *xmu;
    hddvd_xmu_styles *styles;
    hddvd_xmu_styles *pristine;
    struct hddvd_runtime_node *nodes;
    size_t node_count;
    size_t focus;
    /* Counts documents loaded, so element objects can tell they are stale. */
    uint32_t documents;
    uint64_t generation;
    uint64_t now;
    hddvd_runtime_issue issues[HDDVD_RUNTIME_MAX_ISSUES];
    size_t issue_count;
    size_t dropped;
    /* Exceptions recorded by the current host call, and the first one's
       message (owned). */
    size_t call_exceptions;
    char *call_exception;
};

/* runtime.c: runs a prelude entry with budgets, as one host call. */
int hddvd_runtime_run_entry(hddvd_runtime *, const char *entry, const hddvd_engine_value *args,
                            size_t count, double *result);
/* runtime_dom.c */
extern const char *const hddvd_runtime_native_names[];
extern const size_t hddvd_runtime_native_count;
int hddvd_runtime_native(void *opaque, unsigned index, const hddvd_engine_value *args, size_t count,
                         hddvd_engine_value *result, const char **message);
void hddvd_runtime_document_free(hddvd_runtime *);
void hddvd_runtime_issues_free(hddvd_runtime *);
#endif
