#include <hddvd.h>

#include <string_view>

int main()
{
    static_assert(HDDVD_ABI_VERSION == 1u, "unexpected libhddvd ABI");
    return std::string_view(hddvd_feature_name(HDDVD_FEATURE_DISPLAY_LIST)) == "display-list" ? 0
                                                                                              : 1;
}
