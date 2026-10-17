#include "hddvd.h"
#include <stddef.h>
#include <stdint.h>

/* Runs the input as a script (any encoding the runtime accepts), then calls
   its function f, under small budgets so every input finishes quickly. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    hddvd_runtime_limits limits;
    hddvd_runtime *runtime = NULL;
    hddvd_runtime_diagnostic diagnostic;
    hddvd_runtime_limits_default(&limits);
    limits.max_heap_bytes = 4u * 1024u * 1024u;
    limits.max_recursion = 16;
    limits.max_operations = 200000;
    limits.max_milliseconds = 200;
    if (hddvd_runtime_create_with_limits(&limits, &runtime) != HDDVD_OK)
        return 0;
    (void)hddvd_runtime_evaluate(runtime, "fuzz.js", data, size);
    (void)hddvd_runtime_get_diagnostic(runtime, &diagnostic);
    (void)hddvd_runtime_call(runtime, "f");
    (void)hddvd_runtime_get_diagnostic(runtime, &diagnostic);
    hddvd_runtime_destroy(runtime);
    return 0;
}
