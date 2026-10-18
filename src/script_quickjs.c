#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "internal.h"
#include "script_engine.h"
#include <math.h>
#include <quickjs.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

/* QuickJS backend (docs/decisions/0001-script-engine.md). The context gets
   the ECMAScript built-ins only; QuickJS's std and os modules, which reach
   files and processes, are never added. */

/* QuickJS calls the interrupt handler once per this many loop iterations
   and calls (JS_INTERRUPT_COUNTER_INIT in quickjs.c). */
#define POLL_BLOCK 10000u
/* Every allocation carries its size in a header that keeps the alignment
   malloc gives. */
#define HEADER 16u

struct hddvd_engine
{
    JSRuntime *runtime;
    JSContext *context;
    hddvd_runtime_limits limits;
    /* Per evaluate or call. */
    int heap_refused;
    uint64_t operations;
    uint64_t started;
    hddvd_runtime_limit stopped;
    uint64_t random_state;
    /* Host objects: the prelude's entry object and the natives it calls. */
    JSValue host;
    hddvd_engine_native_fn native;
    void *native_opaque;
};

const char *hddvd_engine_name(void)
{
#ifdef HDDVD_QUICKJS_VERSION
    return "QuickJS " HDDVD_QUICKJS_VERSION;
#else
    return "QuickJS";
#endif
}

static uint64_t now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts))
        return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static size_t block_size(const void *block)
{
    size_t size;
    memcpy(&size, (const unsigned char *)block - HEADER, sizeof(size));
    return size;
}

/* Counts the header too, since it is memory the engine holds. */
static void *heap_malloc(JSMallocState *state, size_t size)
{
    hddvd_engine *engine = state->opaque;
    unsigned char *block;
    if (size > SIZE_MAX - HEADER || state->malloc_size > engine->limits.max_heap_bytes ||
        size + HEADER > engine->limits.max_heap_bytes - state->malloc_size)
    {
        engine->heap_refused = 1;
        return NULL;
    }
    block = malloc(size + HEADER);
    if (!block)
        return NULL;
    memcpy(block, &size, sizeof(size));
    state->malloc_count++;
    state->malloc_size += size + HEADER;
    return block + HEADER;
}

static void heap_free(JSMallocState *state, void *ptr)
{
    if (!ptr)
        return;
    state->malloc_count--;
    state->malloc_size -= block_size(ptr) + HEADER;
    free((unsigned char *)ptr - HEADER);
}

static void *heap_realloc(JSMallocState *state, void *ptr, size_t size)
{
    hddvd_engine *engine = state->opaque;
    unsigned char *block;
    size_t old;
    if (!ptr)
        return heap_malloc(state, size);
    if (!size)
    {
        heap_free(state, ptr);
        return NULL;
    }
    old = block_size(ptr);
    if (size > old &&
        (size > SIZE_MAX - HEADER || state->malloc_size > engine->limits.max_heap_bytes ||
         size - old > engine->limits.max_heap_bytes - state->malloc_size))
    {
        engine->heap_refused = 1;
        return NULL;
    }
    block = realloc((unsigned char *)ptr - HEADER, size + HEADER);
    if (!block)
        return NULL;
    memcpy(block, &size, sizeof(size));
    state->malloc_size = state->malloc_size - old + size;
    return block + HEADER;
}

static size_t heap_usable_size(const void *ptr)
{
    return ptr ? block_size(ptr) : 0;
}

static const JSMallocFunctions heap_functions = {heap_malloc, heap_free, heap_realloc,
                                                 heap_usable_size};

static int interrupt(JSRuntime *runtime, void *opaque)
{
    hddvd_engine *engine = opaque;
    (void)runtime;
    engine->operations += POLL_BLOCK;
    if (engine->operations > engine->limits.max_operations)
        engine->stopped = HDDVD_RUNTIME_LIMIT_OPERATIONS;
    else if (now_ms() - engine->started >= engine->limits.max_milliseconds)
        engine->stopped = HDDVD_RUNTIME_LIMIT_TIME;
    return engine->stopped != HDDVD_RUNTIME_LIMIT_NONE;
}

/* xorshift64*, so every runtime produces the same sequence. */
static JSValue random_number(JSContext *context, JSValueConst self, int argc, JSValueConst *argv)
{
    hddvd_engine *engine = JS_GetContextOpaque(context);
    uint64_t x = engine->random_state;
    (void)self;
    (void)argc;
    (void)argv;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    engine->random_state = x;
    return JS_NewFloat64(context, (double)((x * UINT64_C(0x2545f4914f6cdd1d)) >> 11) *
                                      (1.0 / 9007199254740992.0));
}

static int install_random(JSContext *context)
{
    JSValue global = JS_GetGlobalObject(context);
    JSValue math = JS_GetPropertyStr(context, global, "Math");
    int rc = JS_SetPropertyStr(context, math, "random",
                               JS_NewCFunction(context, random_number, "random", 0));
    JS_FreeValue(context, math);
    JS_FreeValue(context, global);
    return rc < 0 ? -1 : 0;
}

void hddvd_engine_destroy(hddvd_engine *engine)
{
    if (!engine)
        return;
    if (engine->context)
    {
        JS_FreeValue(engine->context, engine->host);
        JS_FreeContext(engine->context);
    }
    if (engine->runtime)
        JS_FreeRuntime(engine->runtime);
    free(engine);
}

int hddvd_engine_create(const hddvd_runtime_limits *limits, hddvd_engine **out)
{
    hddvd_engine *engine = calloc(1, sizeof(*engine));
    *out = NULL;
    if (!engine)
        return HDDVD_ENOMEM;
    engine->limits = *limits;
    engine->random_state = UINT64_C(0x9e3779b97f4a7c15);
    engine->host = JS_UNDEFINED;
    engine->runtime = JS_NewRuntime2(&heap_functions, engine);
    if (engine->runtime)
        engine->context = JS_NewContextRaw(engine->runtime);
    if (engine->context)
    {
        JS_SetContextOpaque(engine->context, engine);
        JS_AddIntrinsicBaseObjects(engine->context);
        JS_AddIntrinsicDate(engine->context);
        JS_AddIntrinsicEval(engine->context);
        JS_AddIntrinsicRegExpCompiler(engine->context);
        JS_AddIntrinsicRegExp(engine->context);
        JS_AddIntrinsicJSON(engine->context);
    }
    /* The intrinsics report no failure of their own; a refused allocation
       means the context may be incomplete. */
    if (!engine->context || engine->heap_refused || install_random(engine->context))
    {
        int rc = engine->heap_refused ? HDDVD_ELIMIT : HDDVD_ENOMEM;
        hddvd_engine_destroy(engine);
        return rc;
    }
    JS_SetMaxStackSize(engine->runtime, limits->max_recursion * HDDVD_RUNTIME_FRAME_BYTES);
    JS_SetInterruptHandler(engine->runtime, interrupt, engine);
    *out = engine;
    return HDDVD_OK;
}

static void begin(hddvd_engine *engine)
{
    engine->heap_refused = 0;
    engine->operations = 0;
    engine->stopped = HDDVD_RUNTIME_LIMIT_NONE;
    engine->started = now_ms();
    /* Stack depth is measured from here, whichever thread calls. */
    JS_UpdateStackTop(engine->runtime);
}

static char *copy_string(JSContext *context, JSValueConst value)
{
    const char *text = JS_ToCString(context, value);
    char *copy;
    if (!text)
    {
        JS_FreeValue(context, JS_GetException(context));
        return NULL;
    }
    copy = hddvd_strdup(text);
    JS_FreeCString(context, text);
    return copy;
}

/* Reads an own data property that QuickJS sets on its errors, so no script
   code runs; NULL when it is not a string. */
static char *error_property(JSContext *context, JSValueConst error, const char *name)
{
    JSValue value = JS_GetPropertyStr(context, error, name);
    char *text = NULL;
    if (JS_IsException(value))
        JS_FreeValue(context, JS_GetException(context));
    else if (JS_IsString(value))
        text = copy_string(context, value);
    JS_FreeValue(context, value);
    return text;
}

/* Turns the pending exception into a report. `status` is used when no
   budget ran out. */
static int report_exception(hddvd_engine *engine, int status, hddvd_engine_report *report)
{
    JSContext *context = engine->context;
    JSValue error = JS_GetException(context);
    int is_error = JS_IsError(context, error);
    char *message = is_error ? error_property(context, error, "message") : NULL;
    if (is_error)
        report->trace = error_property(context, error, "stack");
    if (engine->stopped != HDDVD_RUNTIME_LIMIT_NONE)
    {
        report->limit = engine->stopped;
        report->message = hddvd_strdup(engine->stopped == HDDVD_RUNTIME_LIMIT_TIME
                                           ? "time budget exhausted"
                                           : "operation budget exhausted");
        status = HDDVD_ELIMIT;
    }
    else if (engine->heap_refused)
    {
        report->limit = HDDVD_RUNTIME_LIMIT_HEAP;
        report->message = hddvd_strdup("heap budget exhausted");
        status = HDDVD_ELIMIT;
    }
    else if (is_error && message && !strcmp(message, "stack overflow"))
    {
        report->limit = HDDVD_RUNTIME_LIMIT_RECURSION;
        report->message = hddvd_strdup("recursion budget exhausted");
        status = HDDVD_ELIMIT;
    }
    else if (is_error || !JS_IsObject(error))
    {
        /* Error.prototype.toString may be script-defined; it runs under the
           budget that is still in force. */
        report->message = copy_string(context, error);
    }
    else
    {
        report->message = hddvd_strdup("uncaught exception (object)");
    }
    free(message);
    JS_FreeValue(context, error);
    return status;
}

/* A script that catches a refused allocation still ends with the heap
   budget reported. */
static int finish(hddvd_engine *engine, JSValue result, int status, hddvd_engine_report *report)
{
    if (JS_IsException(result))
        return report_exception(engine, status, report);
    JS_FreeValue(engine->context, result);
    if (engine->heap_refused)
    {
        report->limit = HDDVD_RUNTIME_LIMIT_HEAP;
        report->message = hddvd_strdup("heap budget exhausted");
        return HDDVD_ELIMIT;
    }
    return HDDVD_OK;
}

int hddvd_engine_evaluate(hddvd_engine *engine, const char *name, const char *source, size_t size,
                          hddvd_engine_report *report)
{
    JSValue compiled;
    int rc;
    begin(engine);
    compiled = JS_Eval(engine->context, source, size, name,
                       JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled))
        return report_exception(engine, HDDVD_EPARSE, report);
    /* JS_EvalFunction takes ownership of the compiled function. */
    rc = finish(engine, JS_EvalFunction(engine->context, compiled), HDDVD_ESCRIPT, report);
    return rc;
}

int hddvd_engine_call(hddvd_engine *engine, const char *function, hddvd_engine_report *report)
{
    JSContext *context = engine->context;
    JSValue global, callee, result;
    begin(engine);
    global = JS_GetGlobalObject(context);
    /* A getter on the global object may run script code here. */
    callee = JS_GetPropertyStr(context, global, function);
    if (JS_IsException(callee))
    {
        JS_FreeValue(context, global);
        return report_exception(engine, HDDVD_ESCRIPT, report);
    }
    if (!JS_IsFunction(context, callee))
    {
        JS_FreeValue(context, callee);
        JS_FreeValue(context, global);
        report->message = hddvd_strdup("not a global function");
        return HDDVD_ENOTFOUND;
    }
    result = JS_Call(context, callee, global, 0, NULL);
    JS_FreeValue(context, callee);
    JS_FreeValue(context, global);
    return finish(engine, result, HDDVD_ESCRIPT, report);
}

/* Converts an argument for a native. Strings are copied into `held`, freed
   by the caller with JS_FreeCString. */
static int to_value(JSContext *context, JSValueConst value, hddvd_engine_value *out,
                    const char **held)
{
    memset(out, 0, sizeof(*out));
    *held = NULL;
    if (JS_IsUndefined(value))
        out->type = HDDVD_ENGINE_UNDEFINED;
    else if (JS_IsNull(value))
        out->type = HDDVD_ENGINE_NULL;
    else if (JS_IsBool(value))
    {
        out->type = HDDVD_ENGINE_BOOLEAN;
        out->number = JS_ToBool(context, value) ? 1.0 : 0.0;
    }
    else if (JS_IsNumber(value))
    {
        out->type = HDDVD_ENGINE_NUMBER;
        if (JS_ToFloat64(context, &out->number, value))
            return -1;
    }
    else if (JS_IsString(value))
    {
        out->type = HDDVD_ENGINE_STRING;
        *held = JS_ToCStringLen(context, &out->length, value);
        if (!*held)
            return -1;
        out->string = *held;
    }
    else
        out->type = HDDVD_ENGINE_OTHER;
    return 0;
}

static JSValue from_value(JSContext *context, const hddvd_engine_value *value)
{
    switch (value->type)
    {
        case HDDVD_ENGINE_NULL:
            return JS_NULL;
        case HDDVD_ENGINE_BOOLEAN:
            return JS_NewBool(context, value->number != 0.0);
        case HDDVD_ENGINE_NUMBER:
            return JS_NewFloat64(context, value->number);
        case HDDVD_ENGINE_STRING:
            return JS_NewStringLen(context, value->string ? value->string : "", value->length);
        default:
            return JS_UNDEFINED;
    }
}

#define MAX_NATIVE_ARGS 4

static JSValue native_call(JSContext *context, JSValueConst self, int argc, JSValueConst *argv,
                           int magic)
{
    hddvd_engine *engine = JS_GetContextOpaque(context);
    hddvd_engine_value args[MAX_NATIVE_ARGS], result;
    const char *held[MAX_NATIVE_ARGS] = {NULL};
    const char *message = NULL;
    size_t count = argc < MAX_NATIVE_ARGS ? (size_t)argc : MAX_NATIVE_ARGS, i;
    JSValue out = JS_UNDEFINED;
    int rc = 0;
    (void)self;
    for (i = 0; i < count && !rc; i++)
        rc = to_value(context, argv[i], &args[i], &held[i]);
    if (!rc)
    {
        memset(&result, 0, sizeof(result));
        if (engine->native(engine->native_opaque, (unsigned)magic, args, count, &result, &message))
        {
            JSValue error = JS_NewError(context);
            if (!JS_IsException(error))
                JS_DefinePropertyValueStr(
                    context, error, "message",
                    JS_NewString(context, message ? message : "host call failed"),
                    JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
            out = JS_Throw(context, error);
        }
        else
            out = from_value(context, &result);
    }
    else
        out = JS_EXCEPTION;
    for (i = 0; i < count; i++)
        if (held[i])
            JS_FreeCString(context, held[i]);
    return out;
}

int hddvd_engine_install(hddvd_engine *engine, const char *source, size_t size,
                         const char *const *names, size_t count, hddvd_engine_native_fn native,
                         void *opaque, hddvd_engine_report *report)
{
    JSContext *context = engine->context;
    JSValue factory, natives, global, args[2], host;
    size_t i;
    int rc;
    if (count > 0x7fff)
        return HDDVD_EINVAL;
    begin(engine);
    engine->native = native;
    engine->native_opaque = opaque;
    factory = JS_Eval(context, source, size, "<hdi-prelude>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(factory))
        return report_exception(engine, HDDVD_EPARSE, report);
    natives = JS_NewObject(context);
    for (i = 0; i < count && !JS_IsException(natives); i++)
    {
        JSValue function =
            JS_NewCFunctionMagic(context, native_call, names[i], 0, JS_CFUNC_generic_magic, (int)i);
        if (JS_IsException(function) || JS_SetPropertyStr(context, natives, names[i], function) < 0)
        {
            JS_FreeValue(context, natives);
            natives = JS_EXCEPTION;
        }
    }
    if (JS_IsException(natives))
    {
        JS_FreeValue(context, factory);
        return report_exception(engine, HDDVD_ENOMEM, report);
    }
    global = JS_GetGlobalObject(context);
    args[0] = natives;
    args[1] = global;
    host = JS_Call(context, factory, JS_UNDEFINED, 2, args);
    JS_FreeValue(context, factory);
    JS_FreeValue(context, natives);
    JS_FreeValue(context, global);
    if (JS_IsException(host))
        return report_exception(engine, HDDVD_ESCRIPT, report);
    if (!JS_IsObject(host))
    {
        JS_FreeValue(context, host);
        report->message = hddvd_strdup("the prelude returned no entry object");
        return HDDVD_ESCRIPT;
    }
    rc = finish(engine, JS_UNDEFINED, HDDVD_ESCRIPT, report);
    if (rc)
    {
        JS_FreeValue(context, host);
        return rc;
    }
    JS_FreeValue(context, engine->host);
    engine->host = host;
    return HDDVD_OK;
}

int hddvd_engine_invoke(hddvd_engine *engine, const char *entry, const hddvd_engine_value *args,
                        size_t count, double *result, hddvd_engine_report *report)
{
    JSContext *context = engine->context;
    JSValue callee, value, argv[MAX_NATIVE_ARGS];
    size_t i;
    int rc;
    if (result)
        *result = NAN;
    if (!JS_IsObject(engine->host) || count > MAX_NATIVE_ARGS)
        return HDDVD_EINVAL;
    begin(engine);
    /* The entry object is the prelude's own; no script can reach it. */
    callee = JS_GetPropertyStr(context, engine->host, entry);
    if (JS_IsException(callee))
        return report_exception(engine, HDDVD_ESCRIPT, report);
    if (!JS_IsFunction(context, callee))
    {
        JS_FreeValue(context, callee);
        report->message = hddvd_strdup("no such host entry");
        return HDDVD_ENOTFOUND;
    }
    for (i = 0; i < count; i++)
        argv[i] = from_value(context, &args[i]);
    value = JS_Call(context, callee, engine->host, (int)count, argv);
    JS_FreeValue(context, callee);
    for (i = 0; i < count; i++)
        JS_FreeValue(context, argv[i]);
    if (!JS_IsException(value) && result && (JS_IsNumber(value) || JS_IsBool(value)))
    {
        if (JS_IsBool(value))
            *result = JS_ToBool(context, value) ? 1.0 : 0.0;
        else if (JS_ToFloat64(context, result, value))
            *result = NAN;
    }
    rc = finish(engine, value, HDDVD_ESCRIPT, report);
    return rc;
}
