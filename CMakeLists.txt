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

# The script runtime's ECMAScript engine (docs/decisions/0001-script-engine.md).
# "auto" uses QuickJS when it is available; "none" builds without a runtime.
# QuickJS comes either from HDDVD_QUICKJS_SOURCE_DIR, an unpacked release that
# is compiled into libhddvd with its symbols hidden, or from an installed
# libquickjs.a, which shared builds can only use if it was built with -fPIC.
set(HDDVD_SCRIPT_ENGINE "auto" CACHE STRING "Script engine: auto, quickjs or none")
set_property(CACHE HDDVD_SCRIPT_ENGINE PROPERTY STRINGS auto quickjs none)
set(HDDVD_QUICKJS_SOURCE_DIR "" CACHE PATH "QuickJS release source to build into libhddvd")
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(HDDVD_SELECTED_ENGINE none)
set(HDDVD_QUICKJS_MODE "")
if(NOT HDDVD_SCRIPT_ENGINE MATCHES "^(auto|quickjs|none)$")
    message(FATAL_ERROR "HDDVD_SCRIPT_ENGINE must be auto, quickjs or none")
endif()
if(NOT HDDVD_SCRIPT_ENGINE STREQUAL "none" AND HDDVD_QUICKJS_SOURCE_DIR)
    if(NOT EXISTS "${HDDVD_QUICKJS_SOURCE_DIR}/quickjs.c"
       OR NOT EXISTS "${HDDVD_QUICKJS_SOURCE_DIR}/VERSION")
        message(FATAL_ERROR "HDDVD_QUICKJS_SOURCE_DIR has no QuickJS release source")
    endif()
    if(MSVC)
        message(FATAL_ERROR "QuickJS does not build with MSVC")
    endif()
    file(STRINGS "${HDDVD_QUICKJS_SOURCE_DIR}/VERSION" HDDVD_QUICKJS_VERSION LIMIT_COUNT 1)
    set(quickjs_sources)
    # dtoa.c replaced libbf.c in 2024; libbf is only needed for BigFloat,
    # which libhddvd does not enable.
    foreach(file quickjs.c libregexp.c libunicode.c cutils.c dtoa.c)
        if(EXISTS "${HDDVD_QUICKJS_SOURCE_DIR}/${file}")
            list(APPEND quickjs_sources "${HDDVD_QUICKJS_SOURCE_DIR}/${file}")
        endif()
    endforeach()
    add_library(hddvd_quickjs OBJECT ${quickjs_sources})
    set_target_properties(hddvd_quickjs PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden)
    target_compile_definitions(hddvd_quickjs PRIVATE
        _GNU_SOURCE CONFIG_VERSION="${HDDVD_QUICKJS_VERSION}")
    # Third-party code: its warnings and sanitizer findings are not ours to
    # fix. It is always optimized and never instrumented, because unoptimized
    # or instrumented builds multiply its stack frames past the recursion
    # budget (docs/decisions/0001-script-engine.md). libhddvd's own code keeps
    # the build type and any sanitizers.
    target_compile_options(hddvd_quickjs PRIVATE -w -fno-sanitize=all -O2)
    find_package(Threads REQUIRED)
    set(HDDVD_SELECTED_ENGINE quickjs)
    set(HDDVD_QUICKJS_MODE source)
elseif(NOT HDDVD_SCRIPT_ENGINE STREQUAL "none")
    if(HDDVD_SCRIPT_ENGINE STREQUAL "quickjs")
        find_package(QuickJS REQUIRED)
    else()
        find_package(QuickJS)
    endif()
    set(quickjs_usable ${QuickJS_FOUND})
    if(QuickJS_FOUND AND HDDVD_BUILD_SHARED AND NOT APPLE AND NOT WIN32)
        include(CheckCSourceCompiles)
        set(CMAKE_REQUIRED_LIBRARIES QuickJS::QuickJS)
        set(CMAKE_REQUIRED_FLAGS -fPIC)
        set(CMAKE_REQUIRED_LINK_OPTIONS -shared)
        check_c_source_compiles("
            #include <quickjs.h>
            int main(void) { JS_FreeRuntime(JS_NewRuntime()); return 0; }"
            HDDVD_QUICKJS_LINKS_SHARED)
        unset(CMAKE_REQUIRED_LIBRARIES)
        unset(CMAKE_REQUIRED_FLAGS)
        unset(CMAKE_REQUIRED_LINK_OPTIONS)
        if(NOT HDDVD_QUICKJS_LINKS_SHARED)
            set(quickjs_usable FALSE)
            set(quickjs_message "${QuickJS_LIBRARY} cannot be linked into a shared "
                "library (it was not built with -fPIC). Set HDDVD_QUICKJS_SOURCE_DIR to "
                "a QuickJS release, or build static only (HDDVD_BUILD_SHARED=OFF).")
            if(HDDVD_SCRIPT_ENGINE STREQUAL "quickjs")
                message(FATAL_ERROR ${quickjs_message})
            endif()
            message(STATUS ${quickjs_message})
        endif()
    endif()
    if(quickjs_usable)
        set(HDDVD_SELECTED_ENGINE quickjs)
        set(HDDVD_QUICKJS_MODE installed)
    endif()
endif()
message(STATUS "libhddvd script engine: ${HDDVD_SELECTED_ENGINE}")

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
    src/runtime.c
    src/runtime_dom.c
    src/runtime_player.c
    src/scene.c
    src/script_decode.c
    src/stream.c
    src/style.c
    src/title_map.c
    src/uri.c
    src/util.c
    src/vti.c
    src/vts.c
    src/xmf.c
    src/xmu.c
    src/xpl.c
)
if(HDDVD_SELECTED_ENGINE STREQUAL "quickjs")
    list(APPEND HDDVD_SOURCES src/script_quickjs.c)
else()
    list(APPEND HDDVD_SOURCES src/script_none.c)
endif()

# The script runtime's host objects (src/hdi_prelude.js), compiled in.
set(HDDVD_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
    OUTPUT ${HDDVD_GENERATED_DIR}/hdi_prelude.h
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/src/hdi_prelude.js
        -DOUTPUT=${HDDVD_GENERATED_DIR}/hdi_prelude.h -DNAME=hddvd_prelude
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS src/hdi_prelude.js cmake/EmbedFile.cmake
    COMMENT "Embedding hdi_prelude.js"
    VERBATIM)
add_custom_target(hddvd_generated DEPENDS ${HDDVD_GENERATED_DIR}/hdi_prelude.h)

function(hddvd_configure_library target library_type)
    target_compile_features(${target} PUBLIC c_std_99)
    add_dependencies(${target} hddvd_generated)
    target_include_directories(${target} PRIVATE ${HDDVD_GENERATED_DIR})
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${target} PRIVATE LibXml2::LibXml2)
    if(HDDVD_QUICKJS_MODE STREQUAL "source")
        target_sources(${target} PRIVATE $<TARGET_OBJECTS:hddvd_quickjs>)
        target_include_directories(${target} SYSTEM PRIVATE ${HDDVD_QUICKJS_SOURCE_DIR})
        target_compile_definitions(${target} PRIVATE
            HDDVD_QUICKJS_VERSION="${HDDVD_QUICKJS_VERSION}")
        target_link_libraries(${target} PRIVATE Threads::Threads)
        if(UNIX)
            target_link_libraries(${target} PRIVATE m)
        endif()
    elseif(HDDVD_QUICKJS_MODE STREQUAL "installed")
        target_link_libraries(${target} PRIVATE QuickJS::QuickJS)
    endif()
    if(library_type STREQUAL "SHARED")
        target_compile_definitions(${target} PRIVATE HDDVD_BUILD_SHARED)
        target_compile_definitions(${target} INTERFACE HDDVD_USE_SHARED)
        # Keep an installed engine's symbols out of the shared library's
        # exports (a source build compiles them hidden).
        if(HDDVD_QUICKJS_MODE STREQUAL "installed")
            if(APPLE)
                target_link_options(${target} PRIVATE "LINKER:-exported_symbol,_hddvd_*")
            elseif(NOT WIN32)
                target_link_options(${target} PRIVATE "LINKER:--exclude-libs,ALL")
            endif()
        endif()
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

foreach(tool info menuinfo menurun acainfo acaextract scriptdump scriptinfo xplinfo timemap locate vtsinfo)
    add_executable(hddvd-${tool} tools/hddvd-${tool}.c)
    target_link_libraries(hddvd-${tool} PRIVATE hddvd)
endforeach()

if(BUILD_TESTING)
    foreach(test core disc io object uri resource application xpl xmf xmu style aca evo script_decode display events player limits ifo map title_map scene runtime runtime_dom runtime_player vts)
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
    target_compile_definitions(test-vts PRIVATE
        HDDVD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

    # Concurrent use of separate objects; the CI tsan job runs it under
    # ThreadSanitizer. POSIX threads only.
    find_package(Threads)
    if(CMAKE_USE_PTHREADS_INIT)
        add_executable(test-threads tests/test_threads.c)
        target_link_libraries(test-threads PRIVATE hddvd Threads::Threads)
        target_compile_options(test-threads PRIVATE -UNDEBUG)
        target_compile_definitions(test-threads PRIVATE
            HDDVD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        add_test(NAME threads COMMAND test-threads)
    endif()

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
    # The generated time map must agree with its generated VTI record.
    add_test(NAME timemap-fixture
        COMMAND hddvd-timemap ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/timemap/original.vti
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/timemap/original.map)
    set_tests_properties(timemap-fixture PROPERTIES PASS_REGULAR_EXPRESSION
        "evob=1 entries=4 sectors=131 duration=1\\.877s ok")
    # The generated Standard Content title set must agree with its EVO's
    # navigation packs, and a different file must not pass as that EVO.
    add_test(NAME vtsinfo-fixture
        COMMAND hddvd-vtsinfo ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/standard/HV001I01.IFO
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/standard/HV001T01.EVO)
    set_tests_properties(vtsinfo-fixture PROPERTIES PASS_REGULAR_EXPRESSION
        "evo .*sectors=36 navigation-packs=9.*summary ok")
    add_test(NAME vtsinfo-wrong-evo
        COMMAND hddvd-vtsinfo ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/standard/HV001I01.IFO
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/timemap/original.vti)
    set_tests_properties(vtsinfo-wrong-evo PROPERTIES WILL_FAIL TRUE)
    # Commands that link to a cell their PGC lacks.
    add_test(NAME vtsinfo-bad-link
        COMMAND hddvd-vtsinfo ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/standard/bad-link.IFO)
    set_tests_properties(vtsinfo-bad-link PROPERTIES WILL_FAIL TRUE)
    # The generated application runs: both script encodings, markup, the
    # markup loaded handler, a navigation key, a key listener and its timer.
    if(HDDVD_SELECTED_ENGINE STREQUAL "quickjs")
        add_test(NAME menurun-fixture
            COMMAND hddvd-menurun ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/app/app.aca app.xmf
                right enter wait:0.5 wait:0.6)
        set_tests_properties(menurun-fixture PROPERTIES PASS_REGULAR_EXPRESSION
            "handler\n  focus: one.*right\n  focus: two.*0\\.5\n  focus: two.*0\\.6\n  focus: three")
        # With the playlist: the application's title and a player command.
        add_test(NAME menurun-playlist
            COMMAND hddvd-menurun --playlist ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/app/app.xpl
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/app/app.aca app.xmf
                right enter wait:1.1 enter)
        set_tests_properties(menurun-playlist PROPERTIES PASS_REGULAR_EXPRESSION
            "title 1, application 1 of 1.*focus: three.*command: jump title=Feature time=00:00:01:00")
        add_test(NAME menurun-bad-step
            COMMAND hddvd-menurun ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/app/app.aca app.xmf
                sideways)
        set_tests_properties(menurun-bad-step PROPERTIES WILL_FAIL TRUE)
    endif()

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
        # The script inventory must report exactly the host API the original
        # fixture scripts use.
        add_test(NAME hdi-api-inventory
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_hdi_api_inventory.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/inventory_hdi_scripts.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hdi-api
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/hdi_api_inventory_expected.txt)
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
    # The harnesses link a separate static copy of the library built with
    # coverage instrumentation, so libFuzzer can steer inputs into the parsers.
    add_library(hddvd_fuzz STATIC ${HDDVD_SOURCES})
    hddvd_configure_library(hddvd_fuzz STATIC)
    target_compile_options(hddvd_fuzz PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    set(fuzz_targets aca object xmf xmu script xpl navigation timemap uri titlemap display vts)
    if(HDDVD_SELECTED_ENGINE STREQUAL "quickjs")
        list(APPEND fuzz_targets runtime)
    endif()
    foreach(target ${fuzz_targets})
        add_executable(fuzz-${target} tests/fuzz_${target}.c)
        target_link_libraries(fuzz-${target} PRIVATE hddvd_fuzz)
        target_compile_options(fuzz-${target} PRIVATE
            -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz-${target} PRIVATE
            -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()

install(TARGETS
        ${hddvd_library_targets}
        hddvd-info hddvd-menuinfo hddvd-menurun hddvd-acainfo hddvd-acaextract
        hddvd-scriptdump hddvd-scriptinfo hddvd-xplinfo hddvd-timemap hddvd-locate hddvd-vtsinfo
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
# Static consumers link the engine themselves.
set(HDDVD_PC_LIBS_PRIVATE "")
if(HDDVD_QUICKJS_MODE STREQUAL "source")
    set(HDDVD_PC_LIBS_PRIVATE "-lm -lpthread")
elseif(HDDVD_QUICKJS_MODE STREQUAL "installed")
    get_filename_component(quickjs_dir "${QuickJS_LIBRARY}" DIRECTORY)
    set(HDDVD_PC_LIBS_PRIVATE "-L${quickjs_dir} -lquickjs -lm -lpthread")
    if(CMAKE_DL_LIBS)
        string(APPEND HDDVD_PC_LIBS_PRIVATE " -l${CMAKE_DL_LIBS}")
    endif()
endif()
configure_file(libhddvd.pc.in libhddvd.pc @ONLY)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/hddvdConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/hddvdConfigVersion.cmake
    cmake/FindQuickJS.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hddvd
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libhddvd.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
