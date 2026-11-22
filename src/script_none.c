/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
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
int hddvd_engine_install(hddvd_engine *engine, const char *source, size_t size,
                         const char *const *names, size_t count, hddvd_engine_native_fn native,
                         void *opaque, hddvd_engine_report *report)
{
    (void)engine;
    (void)source;
    (void)size;
    (void)names;
    (void)count;
    (void)native;
    (void)opaque;
    report->status = HDDVD_ENOTSUP;
    return HDDVD_ENOTSUP;
}
int hddvd_engine_invoke(hddvd_engine *engine, const char *entry, const hddvd_engine_value *args,
                        size_t count, double *result, hddvd_engine_report *report)
{
    (void)engine;
    (void)entry;
    (void)args;
    (void)count;
    (void)result;
    report->status = HDDVD_ENOTSUP;
    return HDDVD_ENOTSUP;
}
