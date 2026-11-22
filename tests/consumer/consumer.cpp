/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#include <hddvd.h>

#include <string_view>

int main()
{
    static_assert(HDDVD_ABI_VERSION == 1u, "unexpected libhddvd ABI");
    return std::string_view(hddvd_feature_name(HDDVD_FEATURE_DISPLAY_LIST)) == "display-list" ? 0
                                                                                              : 1;
}
