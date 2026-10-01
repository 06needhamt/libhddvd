if(NOT DEFINED HDDVD_SOURCE_DIR OR NOT DEFINED HDDVD_BINARY_DIR)
    message(FATAL_ERROR "source and binary directories are required")
endif()

set(test_root "${HDDVD_BINARY_DIR}/install-consumer")
set(prefix "${test_root}/prefix")
set(consumer_build "${test_root}/build")
file(REMOVE_RECURSE "${test_root}")

set(config_args)
if(NOT HDDVD_CONFIG STREQUAL "")
    list(APPEND config_args --config "${HDDVD_CONFIG}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${HDDVD_BINARY_DIR}"
            --prefix "${prefix}" ${config_args}
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "libhddvd installation failed: ${result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -S "${HDDVD_SOURCE_DIR}/tests/consumer"
            -B "${consumer_build}"
            -G "${HDDVD_GENERATOR}"
            "-DCMAKE_PREFIX_PATH=${prefix}"
            "-DCMAKE_C_FLAGS=${HDDVD_C_FLAGS}"
            "-DCMAKE_CXX_FLAGS=${HDDVD_C_FLAGS}"
            "-DCMAKE_EXE_LINKER_FLAGS=${HDDVD_EXE_LINKER_FLAGS}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "consumer configuration failed: ${result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" ${config_args}
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "consumer build failed: ${result}")
endif()

if(WIN32)
    set(path_separator "\;")
else()
    set(path_separator ":")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PATH=${prefix}/bin${path_separator}$ENV{PATH}"
            "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build}"
            ${config_args} --output-on-failure
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "consumer tests failed: ${result}")
endif()
