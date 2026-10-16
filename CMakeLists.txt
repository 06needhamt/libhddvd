cmake_minimum_required(VERSION 3.16)

project(libhddvd VERSION 0.4.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

option(HDDVD_ENABLE_REFERENCE_TESTS
       "Enable tests using an external reference ACA" OFF)
option(HDDVD_ENABLE_EXTERNAL_MENU_TESTS
       "Enable inventory tests using an external menu ZIP or directory" OFF)
option(HDDVD_BUILD_FUZZERS "Build libFuzzer harnesses (Clang only)" OFF)
option(HDDVD_BUILD_SHARED "Build the shared libhddvd library" ON)
option(HDDVD_BUILD_STATIC "Build the static libhddvd library" ON)

if(NOT HDDVD_BUILD_SHARED AND NOT HDDVD_BUILD_STATIC)
    message(FATAL_ERROR
        "At least one of HDDVD_BUILD_SHARED or HDDVD_BUILD_STATIC must be enabled")
endif()

find_package(LibXml2 REQUIRED)

set(HDDVD_SOURCES
    src/application.c
    src/aca.c
    src/disc.c
    src/display.c
    src/evo.c
    src/hddvd.c
    src/ifo.c
    src/io.c
    src/limits.c
    src/map.c
    src/object.c
    src/player.c
    src/resource.c
    src/script_decode.c
    src/stream.c
    src/style.c
    src/uri.c
    src/util.c
    src/xmf.c
    src/xmu.c
    src/xpl.c
)

function(hddvd_configure_library target library_type)
    target_compile_features(${target} PUBLIC c_std_99)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${target} PRIVATE LibXml2::LibXml2)
    if(library_type STREQUAL "SHARED")
        target_compile_definitions(${target} PRIVATE HDDVD_BUILD_SHARED)
        target_compile_definitions(${target} INTERFACE HDDVD_USE_SHARED)
    endif()
    set_target_properties(${target} PROPERTIES
        C_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endfunction()

set(hddvd_library_targets)
if(HDDVD_BUILD_SHARED)
    add_library(hddvd SHARED ${HDDVD_SOURCES})
    hddvd_configure_library(hddvd SHARED)
    list(APPEND hddvd_library_targets hddvd)
elseif(HDDVD_BUILD_STATIC)
    add_library(hddvd STATIC ${HDDVD_SOURCES})
    hddvd_configure_library(hddvd STATIC)
    list(APPEND hddvd_library_targets hddvd)
endif()
add_library(hddvd::hddvd ALIAS hddvd)

if(HDDVD_BUILD_STATIC AND HDDVD_BUILD_SHARED)
    add_library(hddvd_static STATIC ${HDDVD_SOURCES})
    hddvd_configure_library(hddvd_static STATIC)
    # A shared library's import library also uses the .lib suffix on Windows.
    if(NOT WIN32)
        set_target_properties(hddvd_static PROPERTIES OUTPUT_NAME hddvd)
    endif()
    list(APPEND hddvd_library_targets hddvd_static)
    add_library(hddvd::hddvd_static ALIAS hddvd_static)
elseif(HDDVD_BUILD_STATIC)
    add_library(hddvd::hddvd_static ALIAS hddvd)
endif()

foreach(tool info menuinfo acainfo acaextract scriptdump scriptinfo xplinfo)
    add_executable(hddvd-${tool} tools/hddvd-${tool}.c)
    target_link_libraries(hddvd-${tool} PRIVATE hddvd)
endforeach()

if(BUILD_TESTING)
    foreach(test core disc io object uri resource application xpl xmf xmu style aca evo script_decode display events player limits)
        add_executable(test-${test} tests/test_${test}.c)
        target_link_libraries(test-${test} PRIVATE hddvd)
        # Test assertions also perform setup and must run in Release builds.
        if(MSVC)
            target_compile_options(test-${test} PRIVATE /UNDEBUG)
        else()
            target_compile_options(test-${test} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${test} COMMAND test-${test})
    endforeach()

    target_compile_definitions(test-xmf PRIVATE
        HDDVD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_compile_definitions(test-xmu PRIVATE
        HDDVD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_compile_definitions(test-style PRIVATE
        HDDVD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_compile_definitions(test-xpl PRIVATE
        HDDVD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_compile_definitions(test-limits PRIVATE
        HDDVD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

    if(HDDVD_ENABLE_REFERENCE_TESTS)
        find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
        add_test(NAME reference-menu
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_reference_menu.py
                    $<TARGET_FILE:hddvd-acainfo>
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_menu_expected.json)
        set_tests_properties(reference-menu PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    if(HDDVD_ENABLE_EXTERNAL_MENU_TESTS)
        find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
        add_test(NAME external-menu-corpus
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_external_menu_corpus.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/inventory_external_menus.py
                    $<TARGET_FILE:hddvd-acaextract>
                    $<TARGET_FILE:hddvd-xplinfo>)
        set_tests_properties(external-menu-corpus PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    # Byte accounting must cover every input byte exactly once (P4-01).
    set(scriptinfo_cases
        "scripts/minimal.hdis|summary: size=68 structure=68 observed=0 unknown=0"
        "scripts/gapped.hdis|summary: size=101 structure=97 observed=0 unknown=4"
        "scripts/aacs-protected.bin|summary: size=359 structure=9 observed=2 unknown=348"
        "objects/aacs-text.hdo|summary: size=315 structure=281 observed=6 unknown=28"
        "objects/hdis-script.hdo|summary: size=112 structure=104 observed=0 unknown=8"
        "objects/protected.hdo|summary: size=60 structure=40 observed=0 unknown=20")
    foreach(case IN LISTS scriptinfo_cases)
        string(REPLACE "|" ";" case "${case}")
        list(GET case 0 fixture)
        list(GET case 1 expected)
        get_filename_component(name "${fixture}" NAME_WE)
        add_test(NAME scriptinfo-${name}
            COMMAND hddvd-scriptinfo ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/${fixture})
        set_tests_properties(scriptinfo-${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expected}")
    endforeach()
    add_test(NAME scriptinfo-rejects-invalid
        COMMAND hddvd-scriptinfo ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/objects/bad-flags.hdo)
    set_tests_properties(scriptinfo-rejects-invalid PROPERTIES WILL_FAIL TRUE)

    # The inventory's library check must pass on the original synthetic
    # playlists before it is trusted on an external corpus.
    find_package(Python3 3.8 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME xpl-inventory
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/inventory_external_menus.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                    $<TARGET_FILE:hddvd-acaextract>
                    --xplinfo $<TARGET_FILE:hddvd-xplinfo>
                    --require-modeled)
    endif()

    add_test(NAME install-consumer
        COMMAND ${CMAKE_COMMAND}
            -DHDDVD_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DHDDVD_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DHDDVD_GENERATOR=${CMAKE_GENERATOR}
            -DHDDVD_CONFIG=$<CONFIG>
            "-DHDDVD_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DHDDVD_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DHDDVD_VCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}"
            "-DHDDVD_VCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
            "-DHDDVD_C_FLAGS=${CMAKE_C_FLAGS}"
            "-DHDDVD_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_install.cmake)
    set_tests_properties(install-consumer PROPERTIES TIMEOUT 600)
endif()

if(HDDVD_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "HDDVD_BUILD_FUZZERS requires Clang and libFuzzer")
    endif()
    foreach(target aca object xmf xmu script xpl)
        add_executable(fuzz-${target} tests/fuzz_${target}.c)
        target_link_libraries(fuzz-${target} PRIVATE hddvd)
        target_compile_options(fuzz-${target} PRIVATE
            -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz-${target} PRIVATE
            -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()

install(TARGETS
        ${hddvd_library_targets}
        hddvd-info hddvd-menuinfo hddvd-acainfo hddvd-acaextract
        hddvd-scriptdump hddvd-scriptinfo hddvd-xplinfo
    EXPORT hddvdTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/hddvd.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT hddvdTargets
    NAMESPACE hddvd::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hddvd)

configure_package_config_file(
    cmake/hddvdConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/hddvdConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hddvd
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/hddvdConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_file(libhddvd.pc.in libhddvd.pc @ONLY)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/hddvdConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/hddvdConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hddvd
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libhddvd.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
