#include "script_engine.h"

/* Builds without an ECMAScript engine (HDDVD_SCRIPT_ENGINE=none). */
const char *hddvd_engine_name(void)
{
    return NULL;
}
int hddvd_engine_create(const hddvd_runtime_limits *limits, hddvd_engine **out)
{
    (void)limits;
    *out = NULL;
    return HDDVD_ENOTSUP;
}
void hddvd_engine_destroy(hddvd_engine *engine)
{
    (void)engine;
}
int hddvd_engine_evaluate(hddvd_engine *engine, const char *name, const char *source, size_t size,
                          hddvd_engine_report *report)
{
    (void)engine;
    (void)name;
    (void)source;
    (void)size;
    report->status = HDDVD_ENOTSUP;
    return HDDVD_ENOTSUP;
}
int hddvd_engine_call(hddvd_engine *engine, const char *function, hddvd_engine_report *report)
{
    (void)engine;
    (void)function;
    report->status = HDDVD_ENOTSUP;
    return HDDVD_ENOTSUP;
}
