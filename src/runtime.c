#include "runtime_internal.h"
#include <stdlib.h>
#include <string.h>

/* Engine-neutral half of the script runtime: argument and limit checks,
   source decoding, the host-object prelude, and the last outcome. The engine
   sits behind script_engine.h; the host objects' document state is in
   runtime_dom.c. */

/* hdi_prelude.js, embedded at build time (cmake/EmbedFile.cmake). */
#include "hdi_prelude.h"

const char *hddvd_runtime_engine(void)
{
    return hddvd_engine_name();
}

static void limits_from_config(const hddvd_config_limits *config, hddvd_runtime_limits *limits)
{
    memset(limits, 0, sizeof(*limits));
    limits->max_source_bytes = config->max_script_bytes;
    limits->max_heap_bytes = config->max_script_heap_bytes;
    limits->max_recursion = config->max_script_recursion;
    limits->max_operations = config->max_script_instructions;
    limits->max_milliseconds = 1000;
}

void hddvd_runtime_limits_default(hddvd_runtime_limits *limits)
{
    hddvd_config_limits config;
    if (!limits)
        return;
    hddvd_config_limits_default(&config);
    limits_from_config(&config, limits);
}

static int limits_valid(const hddvd_runtime_limits *limits)
{
    return limits->max_source_bytes && limits->max_heap_bytes && limits->max_recursion &&
           limits->max_operations && limits->max_milliseconds &&
           limits->max_heap_bytes <= SIZE_MAX &&
           limits->max_recursion <= SIZE_MAX / HDDVD_RUNTIME_FRAME_BYTES;
}

int hddvd_runtime_create_with_limits(const hddvd_runtime_limits *limits, hddvd_runtime **out)
{
    hddvd_runtime *runtime;
    int rc;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!limits || !limits_valid(limits))
        return HDDVD_EINVAL;
    if (!hddvd_engine_name())
        return HDDVD_ENOTSUP;
    runtime = calloc(1, sizeof(*runtime));
    if (!runtime)
        return HDDVD_ENOMEM;
    runtime->limits = *limits;
    runtime->focus = SIZE_MAX;
    runtime->player.title = SIZE_MAX;
    runtime->application_self = SIZE_MAX;
    rc = hddvd_engine_create(limits, &runtime->engine);
    if (!rc)
        rc = hddvd_engine_install(runtime->engine, (const char *)hddvd_prelude,
                                  sizeof(hddvd_prelude) - 1, hddvd_runtime_native_names,
                                  hddvd_runtime_native_count, hddvd_runtime_native, runtime,
                                  &runtime->report);
    if (rc)
    {
        hddvd_runtime_destroy(runtime);
        return rc;
    }
    *out = runtime;
    return HDDVD_OK;
}

int hddvd_runtime_create(hddvd_runtime **out)
{
    hddvd_runtime_limits limits;
    hddvd_runtime_limits_default(&limits);
    return hddvd_runtime_create_with_limits(&limits, out);
}

int hddvd_runtime_create_with_config(const hddvd_config_limits *config, hddvd_runtime **out)
{
    hddvd_runtime_limits limits;
    if (!out)
        return HDDVD_EINVAL;
    *out = NULL;
    if (!hddvd_config_limits_valid(config))
        return HDDVD_EINVAL;
    limits_from_config(config, &limits);
    return hddvd_runtime_create_with_limits(&limits, out);
}

static void report_clear(hddvd_engine_report *report)
{
    free(report->message);
    free(report->trace);
    memset(report, 0, sizeof(*report));
}

void hddvd_runtime_destroy(hddvd_runtime *runtime)
{
    if (!runtime)
        return;
    hddvd_engine_destroy(runtime->engine);
    hddvd_runtime_document_free(runtime);
    hddvd_runtime_issues_free(runtime);
    hddvd_runtime_player_free(runtime);
    free(runtime->call_exception);
    report_clear(&runtime->report);
    free(runtime);
}

static int fail(hddvd_engine_report *report, int status, hddvd_runtime_limit limit,
                const char *message)
{
    report->status = status;
    report->limit = limit;
    report->message = hddvd_strdup(message);
    return status;
}

static int invalid_text(hddvd_engine_report *report, const char *encoding, size_t offset)
{
    char message[64];
    snprintf(message, sizeof(message), "invalid %s at byte %lu", encoding, (unsigned long)offset);
    return fail(report, HDDVD_EPARSE, HDDVD_RUNTIME_LIMIT_NONE, message);
}

static size_t put_utf8(char *out, uint32_t c)
{
    if (c < 0x80)
    {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800)
    {
        out[0] = (char)(0xc0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3f));
        return 2;
    }
    if (c < 0x10000)
    {
        out[0] = (char)(0xe0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3f));
        out[2] = (char)(0x80 | (c & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3f));
    out[3] = (char)(0x80 | (c & 0x3f));
    return 4;
}

/* Converts UTF-16 after its byte-order mark. A pair of units becomes four
   bytes and a single unit at most three, so 3 bytes per unit always fit. */
static int from_utf16(const unsigned char *in, size_t size, int big_endian, char **out,
                      size_t *length, hddvd_engine_report *report)
{
    size_t units = size / 2, i, used = 0;
    char *text;
    if (size % 2)
        return invalid_text(report, "UTF-16", size + 1);
    if (units > (SIZE_MAX - 1) / 3)
        return fail(report, HDDVD_ELIMIT, HDDVD_RUNTIME_LIMIT_SOURCE, "source too large");
    text = malloc(units * 3 + 1);
    if (!text)
        return HDDVD_ENOMEM;
    for (i = 0; i < units; i++)
    {
        const unsigned char *p = in + i * 2;
        uint32_t c = big_endian ? (uint32_t)(p[0] << 8 | p[1]) : (uint32_t)(p[1] << 8 | p[0]);
        if (c >= 0xdc00 && c <= 0xdfff)
            break;
        if (c >= 0xd800 && c <= 0xdbff)
        {
            uint32_t low;
            if (i + 1 == units)
                break;
            p += 2;
            low = big_endian ? (uint32_t)(p[0] << 8 | p[1]) : (uint32_t)(p[1] << 8 | p[0]);
            if (low < 0xdc00 || low > 0xdfff)
                break;
            c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
            i++;
        }
        used += put_utf8(text + used, c);
    }
    if (i < units)
    {
        free(text);
        /* +2 counts the byte-order mark. */
        return invalid_text(report, "UTF-16", i * 2 + 2);
    }
    text[used] = '\0';
    *out = text;
    *length = used;
    return HDDVD_OK;
}

/* Length of the UTF-8 sequence at p, or 0 when it is not well formed
   (overlong, surrogate, above U+10FFFF, or truncated). */
static size_t utf8_sequence(const unsigned char *p, size_t left)
{
    size_t n, i;
    uint32_t c;
    if (p[0] < 0x80)
        return 1;
    if (p[0] >= 0xc2 && p[0] <= 0xdf)
        n = 2, c = p[0] & 0x1fu;
    else if (p[0] >= 0xe0 && p[0] <= 0xef)
        n = 3, c = p[0] & 0x0fu;
    else if (p[0] >= 0xf0 && p[0] <= 0xf4)
        n = 4, c = p[0] & 0x07u;
    else
        return 0;
    if (left < n)
        return 0;
    for (i = 1; i < n; i++)
    {
        if ((p[i] & 0xc0) != 0x80)
            return 0;
        c = c << 6 | (p[i] & 0x3fu);
    }
    if ((n == 3 && (c < 0x800 || (c >= 0xd800 && c <= 0xdfff))) || (n == 4 && c < 0x10000) ||
        c > 0x10ffff)
        return 0;
    return n;
}

static int from_utf8(const unsigned char *in, size_t size, size_t skipped, char **out,
                     size_t *length, hddvd_engine_report *report)
{
    size_t i = 0;
    char *text;
    while (i < size)
    {
        size_t n = utf8_sequence(in + i, size - i);
        if (!n)
            return invalid_text(report, "UTF-8", skipped + i);
        i += n;
    }
    if (size == SIZE_MAX)
        return fail(report, HDDVD_ELIMIT, HDDVD_RUNTIME_LIMIT_SOURCE, "source too large");
    text = malloc(size + 1);
    if (!text)
        return HDDVD_ENOMEM;
    if (size)
        memcpy(text, in, size);
    text[size] = '\0';
    *out = text;
    *length = size;
    return HDDVD_OK;
}

static int decode(const void *source, size_t size, char **out, size_t *length,
                  hddvd_engine_report *report)
{
    const unsigned char *in = source;
    if (size >= 2 && in[0] == 0xfe && in[1] == 0xff)
        return from_utf16(in + 2, size - 2, 1, out, length, report);
    if (size >= 2 && in[0] == 0xff && in[1] == 0xfe)
        return from_utf16(in + 2, size - 2, 0, out, length, report);
    if (size >= 3 && in[0] == 0xef && in[1] == 0xbb && in[2] == 0xbf)
        return from_utf8(in + 3, size - 3, 3, out, length, report);
    return from_utf8(in, size, 0, out, length, report);
}

int hddvd_runtime_evaluate(hddvd_runtime *runtime, const char *name, const void *source,
                           size_t size)
{
    char *text = NULL;
    size_t length = 0;
    int rc;
    if (!runtime || (!source && size))
        return HDDVD_EINVAL;
    report_clear(&runtime->report);
    if (size > runtime->limits.max_source_bytes)
        return fail(&runtime->report, HDDVD_ELIMIT, HDDVD_RUNTIME_LIMIT_SOURCE,
                    "source exceeds the source budget");
    rc = size ? decode(source, size, &text, &length, &runtime->report)
              : from_utf8((const unsigned char *)"", 0, 0, &text, &length, &runtime->report);
    if (rc)
    {
        runtime->report.status = rc;
        return rc;
    }
    rc = hddvd_engine_evaluate(runtime->engine, name ? name : "<script>", text, length,
                               &runtime->report);
    free(text);
    runtime->report.status = rc;
    return rc;
}

int hddvd_runtime_call(hddvd_runtime *runtime, const char *function)
{
    int rc;
    if (!runtime || !function || !*function)
        return HDDVD_EINVAL;
    report_clear(&runtime->report);
    rc = hddvd_engine_call(runtime->engine, function, &runtime->report);
    runtime->report.status = rc;
    return rc;
}

int hddvd_runtime_run_entry(hddvd_runtime *runtime, const char *entry,
                            const hddvd_engine_value *args, size_t count, double *result)
{
    int rc;
    report_clear(&runtime->report);
    runtime->call_exceptions = 0;
    free(runtime->call_exception);
    runtime->call_exception = NULL;
    rc = hddvd_engine_invoke(runtime->engine, entry, args, count, result, &runtime->report);
    /* Listeners and timers catch their own exceptions; report the first. */
    if (!rc && runtime->call_exceptions)
    {
        rc = HDDVD_ESCRIPT;
        runtime->report.message = runtime->call_exception;
        runtime->call_exception = NULL;
    }
    runtime->report.status = rc;
    return rc;
}

int hddvd_runtime_get_diagnostic(const hddvd_runtime *runtime, hddvd_runtime_diagnostic *out)
{
    if (!runtime || !out)
        return HDDVD_EINVAL;
    memset(out, 0, sizeof(*out));
    out->status = runtime->report.status;
    out->limit = runtime->report.limit;
    out->message = runtime->report.message;
    out->trace = runtime->report.trace;
    return HDDVD_OK;
}
