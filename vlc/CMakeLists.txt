# The VLC plugin (vlc/README.md). It builds against an installed VLC 4: the
# vlc-plugin pkg-config package where VLC provides it (autotools installs),
# otherwise the plugin headers and libvlccore under CMAKE_PREFIX_PATH (meson
# installs ship no vlc-plugin.pc).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(VLC_PLUGIN QUIET IMPORTED_TARGET vlc-plugin>=4.0)
    pkg_check_modules(LIBVLC QUIET IMPORTED_TARGET libvlc>=4.0)
endif()

add_library(hddvd_vlc_plugin MODULE hddvd.c)
set_target_properties(hddvd_vlc_plugin PROPERTIES
    OUTPUT_NAME hddvd_plugin
    PREFIX lib
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_VISIBILITY_PRESET hidden
    # A directory of its own, so tests can point VLC_PLUGIN_PATH at it.
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugins)
target_compile_definitions(hddvd_vlc_plugin PRIVATE
    MODULE_STRING="hddvd" VLC_DYNAMIC_PLUGIN _FILE_OFFSET_BITS=64)

if(VLC_PLUGIN_FOUND)
    target_link_libraries(hddvd_vlc_plugin PRIVATE PkgConfig::VLC_PLUGIN)
    pkg_get_variable(vlc_plugins_dir vlc-plugin pluginsdir)
else()
    find_path(VLC_PLUGIN_INCLUDE_DIR vlc_plugin.h PATH_SUFFIXES vlc/plugins)
    find_library(VLC_CORE_LIBRARY vlccore)
    if(NOT VLC_PLUGIN_INCLUDE_DIR OR NOT VLC_CORE_LIBRARY)
        message(FATAL_ERROR "HDDVD_BUILD_VLC_PLUGIN needs VLC 4's plugin headers and libvlccore; "
            "add the VLC install prefix to CMAKE_PREFIX_PATH")
    endif()
    get_filename_component(vlc_include_root "${VLC_PLUGIN_INCLUDE_DIR}/../.." ABSOLUTE)
    target_include_directories(hddvd_vlc_plugin PRIVATE
        ${VLC_PLUGIN_INCLUDE_DIR} ${vlc_include_root})
    target_link_libraries(hddvd_vlc_plugin PRIVATE ${VLC_CORE_LIBRARY})
    get_filename_component(vlc_lib_dir "${VLC_CORE_LIBRARY}" DIRECTORY)
    set(vlc_plugins_dir "${vlc_lib_dir}/vlc/plugins")
endif()

# libhddvd goes into the plugin, as VLC links its third-party libraries
# from contrib.
if(TARGET hddvd_static)
    set(hddvd_plugin_library hddvd_static)
else()
    set(hddvd_plugin_library hddvd)
endif()
get_target_property(hddvd_plugin_library_type ${hddvd_plugin_library} TYPE)
if(hddvd_plugin_library_type STREQUAL "STATIC_LIBRARY")
    set_target_properties(${hddvd_plugin_library} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
target_link_libraries(hddvd_vlc_plugin PRIVATE ${hddvd_plugin_library})
# Export only VLC's entry points, not the libhddvd copied in.
if(hddvd_plugin_library_type STREQUAL "STATIC_LIBRARY" AND NOT APPLE AND NOT WIN32)
    target_link_options(hddvd_vlc_plugin PRIVATE "LINKER:--exclude-libs,ALL")
endif()

set(HDDVD_VLC_PLUGIN_DIR "${vlc_plugins_dir}/access" CACHE PATH
    "Where to install the VLC plugin")
install(TARGETS hddvd_vlc_plugin LIBRARY DESTINATION ${HDDVD_VLC_PLUGIN_DIR})

# A headless libvlc run over the synthetic disc in tests/fixtures/standard.
if(BUILD_TESTING)
    if(LIBVLC_FOUND)
        set(libvlc_target PkgConfig::LIBVLC)
        set(libvlc_dir "${LIBVLC_LIBRARY_DIRS}")
    else()
        find_path(LIBVLC_INCLUDE_DIR vlc/vlc.h)
        find_library(LIBVLC_LIBRARY vlc)
        set(libvlc_target ${LIBVLC_LIBRARY})
        get_filename_component(libvlc_dir "${LIBVLC_LIBRARY}" DIRECTORY)
    endif()
    if(NOT libvlc_target)
        message(FATAL_ERROR "the VLC plugin test needs libvlc 4")
    endif()
    add_executable(test-vlc-plugin test_plugin.c)
    set_target_properties(test-vlc-plugin PROPERTIES C_STANDARD 11)
    target_link_libraries(test-vlc-plugin PRIVATE ${libvlc_target})
    if(LIBVLC_INCLUDE_DIR)
        target_include_directories(test-vlc-plugin PRIVATE ${LIBVLC_INCLUDE_DIR})
    endif()
    target_compile_definitions(test-vlc-plugin PRIVATE
        HDDVD_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
    target_compile_options(test-vlc-plugin PRIVATE -UNDEBUG)
    add_test(NAME vlc-plugin COMMAND test-vlc-plugin)
    set_tests_properties(vlc-plugin PROPERTIES
        # libvlc finds libvlccore through the loader path when its install
        # has no run path (meson installs).
        ENVIRONMENT "VLC_PLUGIN_PATH=${CMAKE_CURRENT_BINARY_DIR}/plugins;LD_LIBRARY_PATH=${libvlc_dir}"
        TIMEOUT 120)
    add_dependencies(test-vlc-plugin hddvd_vlc_plugin)
endif()
