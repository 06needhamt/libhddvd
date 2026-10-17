#include "hddvd.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Runs `source` as UTF-8 and returns the status. */
static int run(hddvd_runtime *runtime, const char *source)
{
    return hddvd_runtime_evaluate(runtime, "test.js", source, strlen(source));
}

static hddvd_runtime_diagnostic diagnostic(const hddvd_runtime *runtime)
{
    hddvd_runtime_diagnostic out;
    assert(hddvd_runtime_get_diagnostic(runtime, &out) == HDDVD_OK);
    return out;
}

static int has(const char *text, const char *part)
{
    return text && strstr(text, part) != NULL;
}

static void without_engine(void)
{
    hddvd_runtime *runtime = (hddvd_runtime *)1;
    hddvd_runtime_limits limits;
    hddvd_config_limits config;
    assert((hddvd_get_features() & HDDVD_FEATURE_SCRIPT_RUNTIME) == 0);
    hddvd_runtime_limits_default(&limits);
    assert(hddvd_runtime_create(&runtime) == HDDVD_ENOTSUP && runtime == NULL);
    assert(hddvd_runtime_create_with_limits(&limits, &runtime) == HDDVD_ENOTSUP);
    hddvd_config_limits_default(&config);
    assert(hddvd_runtime_create_with_config(&config, &runtime) == HDDVD_ENOTSUP);
    /* Invalid arguments are still reported as such. */
    limits.max_heap_bytes = 0;
    assert(hddvd_runtime_create_with_limits(&limits, &runtime) == HDDVD_EINVAL);
}

static void limits_and_arguments(void)
{
    hddvd_runtime_limits limits, bad;
    hddvd_config_limits config;
    hddvd_runtime *runtime = NULL;
    hddvd_runtime_diagnostic out;

    hddvd_config_limits_default(&config);
    hddvd_runtime_limits_default(&limits);
    assert(limits.max_source_bytes == config.max_script_bytes);
    assert(limits.max_heap_bytes == config.max_script_heap_bytes);
    assert(limits.max_recursion == config.max_script_recursion);
    assert(limits.max_operations == config.max_script_instructions);
    assert(limits.max_milliseconds == 1000);
    hddvd_runtime_limits_default(NULL);

    assert(hddvd_runtime_create(NULL) == HDDVD_EINVAL);
    assert(hddvd_runtime_create_with_limits(NULL, &runtime) == HDDVD_EINVAL);
    bad = limits;
    bad.max_operations = 0;
    assert(hddvd_runtime_create_with_limits(&bad, &runtime) == HDDVD_EINVAL);
    bad = limits;
    bad.max_milliseconds = 0;
    assert(hddvd_runtime_create_with_limits(&bad, &runtime) == HDDVD_EINVAL);
    bad = limits;
    bad.max_recursion = SIZE_MAX;
    assert(hddvd_runtime_create_with_limits(&bad, &runtime) == HDDVD_EINVAL);
    config.max_script_recursion = 0;
    assert(hddvd_runtime_create_with_config(&config, &runtime) == HDDVD_EINVAL);
    assert(hddvd_runtime_create_with_config(NULL, &runtime) == HDDVD_EINVAL);
    assert(runtime == NULL);
    /* Heap too small for an empty context. */
    bad = limits;
    bad.max_heap_bytes = 4096;
    assert(hddvd_runtime_create_with_limits(&bad, &runtime) == HDDVD_ELIMIT);

    assert(hddvd_runtime_create(&runtime) == HDDVD_OK);
    assert(hddvd_runtime_evaluate(NULL, "x", "1", 1) == HDDVD_EINVAL);
    assert(hddvd_runtime_evaluate(runtime, "x", NULL, 1) == HDDVD_EINVAL);
    assert(hddvd_runtime_evaluate(runtime, NULL, NULL, 0) == HDDVD_OK);
    assert(hddvd_runtime_call(NULL, "f") == HDDVD_EINVAL);
    assert(hddvd_runtime_call(runtime, NULL) == HDDVD_EINVAL);
    assert(hddvd_runtime_call(runtime, "") == HDDVD_EINVAL);
    assert(hddvd_runtime_get_diagnostic(NULL, &out) == HDDVD_EINVAL);
    assert(hddvd_runtime_get_diagnostic(runtime, NULL) == HDDVD_EINVAL);
    hddvd_runtime_destroy(runtime);
    hddvd_runtime_destroy(NULL);
}

static void evaluate_and_call(void)
{
    hddvd_runtime *runtime = NULL;
    hddvd_runtime_diagnostic out;
    assert(hddvd_runtime_create(&runtime) == HDDVD_OK);

    assert(run(runtime, "var total = 0; function add() { total = total + 3; }") == HDDVD_OK);
    out = diagnostic(runtime);
    assert(out.status == HDDVD_OK && out.limit == HDDVD_RUNTIME_LIMIT_NONE && !out.message);
    assert(hddvd_runtime_call(runtime, "add") == HDDVD_OK);
    assert(hddvd_runtime_call(runtime, "add") == HDDVD_OK);
    assert(run(runtime, "if (total !== 6) throw new Error('total ' + total);") == HDDVD_OK);

    assert(hddvd_runtime_call(runtime, "missing") == HDDVD_ENOTFOUND);
    assert(hddvd_runtime_call(runtime, "total") == HDDVD_ENOTFOUND);
    out = diagnostic(runtime);
    assert(out.status == HDDVD_ENOTFOUND && out.message);

    assert(run(runtime, "function broken( {") == HDDVD_EPARSE);
    out = diagnostic(runtime);
    assert(out.status == HDDVD_EPARSE && has(out.message, "SyntaxError"));
    /* Nothing from a script with a syntax error runs. */
    assert(run(runtime, "var ran = 1; )") == HDDVD_EPARSE);
    assert(run(runtime, "if (typeof ran !== 'undefined') throw 1;") == HDDVD_OK);

    assert(run(runtime, "function boom() { throw new TypeError('boom'); }") == HDDVD_OK);
    assert(hddvd_runtime_call(runtime, "boom") == HDDVD_ESCRIPT);
    out = diagnostic(runtime);
    assert(out.status == HDDVD_ESCRIPT && out.limit == HDDVD_RUNTIME_LIMIT_NONE);
    assert(has(out.message, "TypeError: boom") && has(out.trace, "test.js"));
    assert(run(runtime, "throw 'plain';") == HDDVD_ESCRIPT);
    assert(has(diagnostic(runtime).message, "plain"));
    assert(run(runtime, "throw {};") == HDDVD_ESCRIPT);
    assert(has(diagnostic(runtime).message, "object"));
    /* A script-defined toString cannot hide the failure. */
    assert(run(runtime, "Error.prototype.toString = function () { throw 1; };"
                        "throw new Error('x');") == HDDVD_ESCRIPT);

    /* A later success clears the diagnostic. */
    assert(run(runtime, "1;") == HDDVD_OK);
    out = diagnostic(runtime);
    assert(out.status == HDDVD_OK && !out.message && !out.trace);
    hddvd_runtime_destroy(runtime);
}

static void encodings(void)
{
    /* "var s = 'é𝄞';" with the characters themselves. */
    static const unsigned char utf16be[] = {0xfe, 0xff, 0,    'v',  0,    'a',  0, 'r',  0, ' ',
                                            0,    's',  0,    ' ',  0,    '=',  0, ' ',  0, '\'',
                                            0x00, 0xe9, 0xd8, 0x34, 0xdd, 0x1e, 0, '\'', 0, ';'};
    static const unsigned char utf16le[] = {0xff, 0xfe, 't', 0, '=', 0, '2', 0, ';', 0};
    static const unsigned char utf8bom[] = {0xef, 0xbb, 0xbf, 'u', '=', '3', ';'};
    static const unsigned char odd[] = {0xfe, 0xff, 0, 'x', 0};
    static const unsigned char lone_high[] = {0xfe, 0xff, 0xd8, 0x34, 0, 'x'};
    static const unsigned char lone_low[] = {0xff, 0xfe, 0x1e, 0xdd};
    static const unsigned char overlong[] = {'x', 0xc0, 0x80};
    static const unsigned char surrogate[] = {0xed, 0xa0, 0x80};
    static const unsigned char truncated[] = {'1', 0xe2, 0x82};
    hddvd_runtime *runtime = NULL;
    assert(hddvd_runtime_create(&runtime) == HDDVD_OK);
    assert(hddvd_runtime_evaluate(runtime, "be", utf16be, sizeof(utf16be)) == HDDVD_OK);
    assert(hddvd_runtime_evaluate(runtime, "le", utf16le, sizeof(utf16le)) == HDDVD_OK);
    assert(hddvd_runtime_evaluate(runtime, "bom", utf8bom, sizeof(utf8bom)) == HDDVD_OK);
    assert(run(runtime, "if (s !== '\\u00e9\\ud834\\udd1e' || t !== 2 || u !== 3) throw 1;") ==
           HDDVD_OK);
    assert(run(runtime, "if ('\xc3\xa9' !== '\\u00e9') throw 1;") == HDDVD_OK);

    assert(hddvd_runtime_evaluate(runtime, "x", odd, sizeof(odd)) == HDDVD_EPARSE);
    assert(has(diagnostic(runtime).message, "UTF-16"));
    assert(hddvd_runtime_evaluate(runtime, "x", lone_high, sizeof(lone_high)) == HDDVD_EPARSE);
    assert(has(diagnostic(runtime).message, "byte 2"));
    assert(hddvd_runtime_evaluate(runtime, "x", lone_low, sizeof(lone_low)) == HDDVD_EPARSE);
    assert(hddvd_runtime_evaluate(runtime, "x", overlong, sizeof(overlong)) == HDDVD_EPARSE);
    assert(has(diagnostic(runtime).message, "UTF-8 at byte 1"));
    assert(hddvd_runtime_evaluate(runtime, "x", surrogate, sizeof(surrogate)) == HDDVD_EPARSE);
    assert(hddvd_runtime_evaluate(runtime, "x", truncated, sizeof(truncated)) == HDDVD_EPARSE);
    hddvd_runtime_destroy(runtime);
}

static void sandbox(void)
{
    hddvd_runtime *a = NULL, *b = NULL;
    assert(hddvd_runtime_create(&a) == HDDVD_OK && hddvd_runtime_create(&b) == HDDVD_OK);
    /* Only the ECMAScript built-ins: no QuickJS std/os modules, no module
       loader, and nothing past what HDi's ECMAScript 3 scripts need. */
    assert(run(a, "if (typeof std !== 'undefined' || typeof os !== 'undefined' ||"
                  "    typeof require !== 'undefined' || typeof print !== 'undefined' ||"
                  "    typeof Promise !== 'undefined' || typeof Proxy !== 'undefined')"
                  "  throw new Error('unexpected global');"
                  "if (typeof JSON !== 'object' || typeof RegExp !== 'function' ||"
                  "    typeof Date !== 'function' || typeof eval !== 'function')"
                  "  throw new Error('missing built-in');") == HDDVD_OK);
    /* Math.random gives the same sequence in every runtime: the first value
       of xorshift64* from the documented seed. */
    {
        uint64_t x = UINT64_C(0x9e3779b97f4a7c15);
        char script[128];
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        snprintf(script, sizeof(script), "if (Math.random() !== %.17g) throw 1;",
                 (double)((x * UINT64_C(0x2545f4914f6cdd1d)) >> 11) / 9007199254740992.0);
        assert(run(a, script) == HDDVD_OK);
        assert(run(b, script) == HDDVD_OK);
        assert(run(a, "for (var i = 0; i < 1000; i++) {"
                      "  var r = Math.random(); if (!(r >= 0 && r < 1)) throw 1; }") == HDDVD_OK);
    }
    hddvd_runtime_destroy(a);
    hddvd_runtime_destroy(b);
}

static hddvd_runtime *create(uint64_t heap, size_t recursion, uint64_t operations,
                             uint32_t milliseconds)
{
    hddvd_runtime_limits limits;
    hddvd_runtime *runtime = NULL;
    hddvd_runtime_limits_default(&limits);
    if (heap)
        limits.max_heap_bytes = heap;
    if (recursion)
        limits.max_recursion = recursion;
    if (operations)
        limits.max_operations = operations;
    if (milliseconds)
        limits.max_milliseconds = milliseconds;
    assert(hddvd_runtime_create_with_limits(&limits, &runtime) == HDDVD_OK);
    return runtime;
}

static void expect_limit(hddvd_runtime *runtime, int rc, hddvd_runtime_limit limit)
{
    hddvd_runtime_diagnostic out = diagnostic(runtime);
    assert(rc == HDDVD_ELIMIT);
    assert(out.status == HDDVD_ELIMIT && out.limit == limit && has(out.message, "budget"));
    /* The runtime keeps working afterwards. */
    assert(run(runtime, "var after = 1 + 1;") == HDDVD_OK);
}

static void budgets(void)
{
    hddvd_runtime *runtime;
    hddvd_runtime_limits limits;

    /* Operations: a loop runs out, even inside try/catch. */
    runtime = create(0, 0, 100000, 60000);
    assert(run(runtime, "var n = 0; for (var i = 0; i < 1000; i++) n++;") == HDDVD_OK);
    expect_limit(runtime, run(runtime, "for (;;) {}"), HDDVD_RUNTIME_LIMIT_OPERATIONS);
    expect_limit(runtime, run(runtime, "try { for (;;) {} } catch (e) {}"),
                 HDDVD_RUNTIME_LIMIT_OPERATIONS);
    /* Calls count too. */
    assert(run(runtime, "function spin() { for (;;) spin2(); } function spin2() {}") == HDDVD_OK);
    expect_limit(runtime, hddvd_runtime_call(runtime, "spin"), HDDVD_RUNTIME_LIMIT_OPERATIONS);
    /* The budget is per call, not cumulative. */
    assert(run(runtime, "for (var i = 0; i < 50000; i++) {}") == HDDVD_OK);
    assert(run(runtime, "for (var i = 0; i < 50000; i++) {}") == HDDVD_OK);
    hddvd_runtime_destroy(runtime);

    /* Wall-clock time. */
    runtime = create(0, 0, UINT64_MAX, 20);
    expect_limit(runtime, run(runtime, "for (;;) {}"), HDDVD_RUNTIME_LIMIT_TIME);
    hddvd_runtime_destroy(runtime);

    /* Recursion: the guaranteed depth succeeds, runaway recursion stops. */
    runtime = create(0, 32, 0, 0);
    assert(run(runtime, "function depth(n) { return n ? 1 + depth(n - 1) : 0; }"
                        "if (depth(32) !== 32) throw 1;") == HDDVD_OK);
    /* Recursion through a native callback has the largest frames. */
    assert(run(runtime, "function mapped(n) {"
                        "  return n ? [n].map(function (x) { return 1 + mapped(x - 1); })[0] : 0; }"
                        "if (mapped(32) !== 32) throw 1;") == HDDVD_OK);
    expect_limit(runtime, run(runtime, "depth(1000000);"), HDDVD_RUNTIME_LIMIT_RECURSION);
    expect_limit(runtime, run(runtime, "mapped(1000000);"), HDDVD_RUNTIME_LIMIT_RECURSION);
    assert(run(runtime, "if (depth(32) !== 32) throw 1;") == HDDVD_OK);
    hddvd_runtime_destroy(runtime);

    /* Heap, including an allocation failure the script catches. */
    runtime = create(4u * 1024u * 1024u, 0, UINT64_MAX, 60000);
    assert(run(runtime,
               "function grow() { var a = []; for (;;) a.push('x' + a.length); }"
               "function hide() { try { grow(); } catch (e) { caught = true; } }") == HDDVD_OK);
    expect_limit(runtime, hddvd_runtime_call(runtime, "grow"), HDDVD_RUNTIME_LIMIT_HEAP);
    expect_limit(runtime, hddvd_runtime_call(runtime, "hide"), HDDVD_RUNTIME_LIMIT_HEAP);
    assert(run(runtime, "if (caught !== true) throw 1;") == HDDVD_OK);
    hddvd_runtime_destroy(runtime);

    /* Source size. */
    hddvd_runtime_limits_default(&limits);
    limits.max_source_bytes = 4;
    assert(hddvd_runtime_create_with_limits(&limits, &runtime) == HDDVD_OK);
    assert(run(runtime, "1;") == HDDVD_OK);
    assert(run(runtime, "1 + 1;") == HDDVD_ELIMIT);
    assert(diagnostic(runtime).limit == HDDVD_RUNTIME_LIMIT_SOURCE);
    hddvd_runtime_destroy(runtime);
}

int main(void)
{
    hddvd_runtime_limits limits;
    hddvd_runtime_limits_default(&limits);
    assert(hddvd_strerror(HDDVD_ESCRIPT) &&
           strcmp(hddvd_strerror(HDDVD_ESCRIPT), "unknown error") != 0);
    if (!hddvd_runtime_engine())
    {
        without_engine();
        return 0;
    }
    assert(hddvd_get_features() & HDDVD_FEATURE_SCRIPT_RUNTIME);
    limits_and_arguments();
    evaluate_and_call();
    encodings();
    sandbox();
    budgets();
    return 0;
}
