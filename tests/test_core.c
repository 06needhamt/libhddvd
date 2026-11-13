#include "hddvd.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    hddvd_features features;
    assert(strcmp(hddvd_version(), "0.6.0") == 0);
    assert(hddvd_abi_version() == HDDVD_ABI_VERSION);
    features = hddvd_get_features();
    assert((features & HDDVD_FEATURE_OBJECT_ENVELOPE) != 0);
    assert((features & HDDVD_FEATURE_DISPLAY_LIST) != 0);
    /* Set exactly when the build has a script engine. */
    assert(((features & HDDVD_FEATURE_SCRIPT_RUNTIME) != 0) == (hddvd_runtime_engine() != NULL));
    assert((features & HDDVD_FEATURE_IMAGE_DECODER) == 0);
    assert((features & HDDVD_FEATURE_TIME_MAP) != 0);
    assert(strcmp(hddvd_feature_name(HDDVD_FEATURE_TIME_MAP), "time-map") == 0);
    assert((features & HDDVD_FEATURE_TITLES) != 0);
    assert(strcmp(hddvd_feature_name(HDDVD_FEATURE_TITLES), "titles") == 0);
    assert(strcmp(hddvd_feature_name(HDDVD_FEATURE_NAV_SCHEDULER), "navigation-scheduler") == 0);
    assert(hddvd_feature_name((hddvd_feature)(UINT64_C(1) << 63)) == NULL);
    assert(strcmp(hddvd_strerror(HDDVD_OK), "success") == 0);
    assert(strcmp(hddvd_strerror(HDDVD_ESCRIPT), "uncaught script exception") == 0);
    assert(hddvd_open(NULL) == NULL);
    return 0;
}
