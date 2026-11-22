# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Tom Needham
cmake_minimum_required(VERSION 3.16)
project(hddvd_consumer LANGUAGES C CXX)

find_package(hddvd CONFIG REQUIRED)

add_executable(hddvd-consumer-c consumer.c)
target_link_libraries(hddvd-consumer-c PRIVATE hddvd::hddvd)

add_executable(hddvd-consumer-cxx consumer.cpp)
target_compile_features(hddvd-consumer-cxx PRIVATE cxx_std_17)
target_link_libraries(hddvd-consumer-cxx PRIVATE hddvd::hddvd)

if(TARGET hddvd::hddvd_static)
    add_executable(hddvd-consumer-static consumer.c)
    target_link_libraries(hddvd-consumer-static PRIVATE hddvd::hddvd_static)
endif()

enable_testing()
add_test(NAME consumer-c COMMAND hddvd-consumer-c)
add_test(NAME consumer-cxx COMMAND hddvd-consumer-cxx)
set(consumer_tests consumer-c consumer-cxx)
if(TARGET hddvd::hddvd_static)
    add_test(NAME consumer-static COMMAND hddvd-consumer-static)
    list(APPEND consumer_tests consumer-static)
endif()

set_tests_properties(${consumer_tests} PROPERTIES TIMEOUT 30)
