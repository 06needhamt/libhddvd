if(NOT DEFINED HDDVD_SOURCE_DIR OR NOT DEFINED HDDVD_BINARY_DIR)
    message(FATAL_ERROR "source and binary directories are required")
endif()

set(test_root "${HDDVD_BINARY_DIR}/install-consumer")
set(prefix "${test_root}/prefix")
set(consumer_build "${test_root}/build")
file(REMOVE_RECURSE "${test_root}")

set(config_args)
set(ctest_config_args)
if(NOT HDDVD_CONFIG STREQUAL "")
    list(APPEND config_args --config "${HDDVD_CONFIG}")
    # CTest uses -C; --config is only valid for cmake --build/--install.
    list(APPEND ctest_config_args -C "${HDDVD_CONFIG}")
endif()

message(STATUS "install-consumer: installing library")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${HDDVD_BINARY_DIR}"
            --prefix "${prefix}" ${config_args}
    TIMEOUT 60
    RESULT_VARIABLE result)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "libhddvd installation failed: ${result}")
endif()

# Reuse the dependency toolchain (notably vcpkg on Windows).
set(consumer_args)
if(HDDVD_TOOLCHAIN_FILE)
    list(APPEND consumer_args "-DCMAKE_TOOLCHAIN_FILE=${HDDVD_TOOLCHAIN_FILE}")
endif()
if(HDDVD_GENERATOR_PLATFORM)
    list(APPEND consumer_args -A "${HDDVD_GENERATOR_PLATFORM}")
endif()
if(HDDVD_VCPKG_TARGET_TRIPLET)
    list(APPEND consumer_args "-DVCPKG_TARGET_TRIPLET=${HDDVD_VCPKG_TARGET_TRIPLET}")
endif()

message(STATUS "install-consumer: configuring consumers")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            ${consumer_args}
            -S "${HDDVD_SOURCE_DIR}/tests/consumer"
            -B "${consumer_build}"
            -G "${HDDVD_GENERATOR}"
            "-DCMAKE_PREFIX_PATH=${prefix}"
            "-DCMAKE_C_FLAGS=${HDDVD_C_FLAGS}"
            "-DCMAKE_CXX_FLAGS=${HDDVD_C_FLAGS}"
            "-DCMAKE_EXE_LINKER_FLAGS=${HDDVD_EXE_LINKER_FLAGS}"
    TIMEOUT 180
    RESULT_VARIABLE result)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "consumer configuration failed: ${result}")
endif()

message(STATUS "install-consumer: building consumers")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" ${config_args}
    TIMEOUT 180
    RESULT_VARIABLE result)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "consumer build failed: ${result}")
endif()

# The installed DLL needs its third-party DLLs too. vcpkg's app-local
# deployment in the original build does not install them into this prefix.
# Set the environment directly to preserve Windows PATH semicolons literally.
if(WIN32)
    set(runtime_path "${prefix}/bin")
    if(HDDVD_VCPKG_INSTALLED_DIR AND HDDVD_VCPKG_TARGET_TRIPLET)
        set(vcpkg_runtime "${HDDVD_VCPKG_INSTALLED_DIR}/${HDDVD_VCPKG_TARGET_TRIPLET}")
        if(HDDVD_CONFIG STREQUAL "Debug")
            string(APPEND vcpkg_runtime "/debug")
        endif()
        string(APPEND vcpkg_runtime "/bin")
        string(APPEND runtime_path ";${vcpkg_runtime}")
        message(STATUS "install-consumer: dependency DLL directory: ${vcpkg_runtime}")
    endif()
    set(ENV{PATH} "${runtime_path};$ENV{PATH}")
else()
    set(ENV{PATH} "${prefix}/bin:$ENV{PATH}")
endif()

message(STATUS "install-consumer: running consumers (30-second timeout per test)")
execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build}"
            ${ctest_config_args} --verbose --output-on-failure
            --interactive-debug-mode 0 --timeout 30
    TIMEOUT 90
    RESULT_VARIABLE result)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "consumer tests failed: ${result}; see ${consumer_build}/Testing/Temporary/LastTest.log")
endif()
