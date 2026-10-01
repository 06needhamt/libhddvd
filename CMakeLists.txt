cmake_minimum_required(VERSION 3.16)

project(libhddvd VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)

option(HDDVD_ENABLE_REFERENCE_TESTS
       "Enable tests using an external reference ACA" OFF)
option(HDDVD_BUILD_FUZZERS "Build libFuzzer harnesses (Clang only)" OFF)

find_package(LibXml2 REQUIRED)

add_library(hddvd
    src/aca.c
    src/disc.c
    src/display.c
    src/evo.c
    src/hddvd.c
    src/ifo.c
    src/io.c
    src/map.c
    src/object.c
    src/resource.c
    src/script_decode.c
    src/stream.c
    src/uri.c
    src/util.c
    src/xmf.c
    src/xpl.c
)
add_library(hddvd::hddvd ALIAS hddvd)

target_compile_features(hddvd PUBLIC c_std_99)
target_include_directories(hddvd PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(hddvd PRIVATE LibXml2::LibXml2)
set_target_properties(hddvd PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(MSVC)
    target_compile_options(hddvd PRIVATE /W4)
else()
    target_compile_options(hddvd PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

foreach(tool info menuinfo acainfo acaextract scriptdump)
    add_executable(hddvd-${tool} tools/hddvd-${tool}.c)
    target_link_libraries(hddvd-${tool} PRIVATE hddvd)
endforeach()

if(BUILD_TESTING)
    foreach(test core io object uri xpl xmf aca evo script_decode display)
        add_executable(test-${test} tests/test_${test}.c)
        target_link_libraries(test-${test} PRIVATE hddvd)
        add_test(NAME ${test} COMMAND test-${test})
    endforeach()

    target_compile_definitions(test-xmf PRIVATE
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
endif()

if(HDDVD_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "HDDVD_BUILD_FUZZERS requires Clang and libFuzzer")
    endif()
    foreach(target aca object xmf xmu script)
        add_executable(fuzz-${target} tests/fuzz_${target}.c)
        target_link_libraries(fuzz-${target} PRIVATE hddvd)
        target_compile_options(fuzz-${target} PRIVATE
            -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz-${target} PRIVATE
            -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()

install(TARGETS
        hddvd hddvd-info hddvd-menuinfo hddvd-acainfo hddvd-acaextract
        hddvd-scriptdump
    EXPORT hddvdTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/hddvd.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT hddvdTargets
    NAMESPACE hddvd::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hddvd)
