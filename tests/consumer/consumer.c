#include <hddvd.h>

#include <string.h>

int main(void)
{
    hddvd_features features = hddvd_get_features();

    if (hddvd_abi_version() != HDDVD_ABI_VERSION)
        return 1;
    if (strcmp(hddvd_version(), "0.4.0") != 0)
        return 2;
    if ((features & HDDVD_FEATURE_OBJECT_ENVELOPE) == 0)
        return 3;
    if (((features & HDDVD_FEATURE_SCRIPT_RUNTIME) != 0) != (hddvd_runtime_engine() != NULL))
        return 4;
    /* A build with an engine must run a script once installed, which also
       checks that static consumers link the engine. */
    if (hddvd_runtime_engine())
    {
        hddvd_runtime *runtime = NULL;
        int rc = hddvd_runtime_create(&runtime);
        if (rc == HDDVD_OK)
            rc = hddvd_runtime_evaluate(runtime, "consumer", "var x = 1 + 1;", 14);
        hddvd_runtime_destroy(runtime);
        if (rc != HDDVD_OK)
            return 5;
    }
    return 0;
}
