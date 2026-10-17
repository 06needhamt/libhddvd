#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "internal.h"
#include "script_engine.h"
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
        JS_FreeContext(engine->context);
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
