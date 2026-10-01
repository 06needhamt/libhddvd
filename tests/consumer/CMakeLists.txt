cmake_minimum_required(VERSION 3.16)
project(hddvd_consumer LANGUAGES C CXX)

find_package(hddvd CONFIG REQUIRED)

add_executable(hddvd-consumer-c consumer.c)
target_link_libraries(hddvd-consumer-c PRIVATE hddvd::hddvd)

add_executable(hddvd-consumer-cxx consumer.cpp)
target_link_libraries(hddvd-consumer-cxx PRIVATE hddvd::hddvd)

enable_testing()
add_test(NAME consumer-c COMMAND hddvd-consumer-c)
add_test(NAME consumer-cxx COMMAND hddvd-consumer-cxx)
