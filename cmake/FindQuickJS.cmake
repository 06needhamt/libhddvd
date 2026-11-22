# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Tom Needham
# Finds QuickJS as installed by its own Makefile or by distributions:
# <prefix>/include/quickjs/quickjs.h and <prefix>/lib/quickjs/libquickjs.a.
# Defines QuickJS::QuickJS. The QuickJS-ng fork has a different API and is
# not supported.
find_path(QuickJS_INCLUDE_DIR quickjs.h PATH_SUFFIXES quickjs)
find_library(QuickJS_LIBRARY NAMES quickjs PATH_SUFFIXES quickjs)
mark_as_advanced(QuickJS_INCLUDE_DIR QuickJS_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(QuickJS
    REQUIRED_VARS QuickJS_LIBRARY QuickJS_INCLUDE_DIR)

if(QuickJS_FOUND AND NOT TARGET QuickJS::QuickJS)
    find_package(Threads REQUIRED)
    add_library(QuickJS::QuickJS UNKNOWN IMPORTED)
    set(quickjs_dependencies Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        list(APPEND quickjs_dependencies m)
    endif()
    set_target_properties(QuickJS::QuickJS PROPERTIES
        IMPORTED_LOCATION "${QuickJS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${QuickJS_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${quickjs_dependencies}")
    unset(quickjs_dependencies)
endif()
