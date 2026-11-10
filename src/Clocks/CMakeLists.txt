cmake_minimum_required(VERSION 3.16)
project(Clocks_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

include("${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/fiesta_host_analysis.cmake")

include(CTest)

set(CLOCKS_SRC ${CMAKE_CURRENT_SOURCE_DIR})
set(HAL_LIB_EXTERNAL ${CMAKE_CURRENT_SOURCE_DIR}/../JaszczurHAL)
set(HAL_SRC ${HAL_LIB_EXTERNAL}/src)
set(CANDEFS ${CMAKE_CURRENT_SOURCE_DIR}/../common/canDefinitions)
set(SCDEFS  ${CMAKE_CURRENT_SOURCE_DIR}/../common/scDefinitions)

include(${HAL_LIB_EXTERNAL}/cmake/jh_managed_frameworks.cmake)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/fiesta_hal_command_sources.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/fiesta_hal_helper_sources.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/fiesta_test_support.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/fiesta_hardware.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/fiesta_shared_sources.cmake")
fiesta_shared_sources(TEST_RUNNER_SOURCES "${FIESTA_SHARED_ROOT}" test_runner)
# jh_hardware.h of the firmware assembly, loaded first like in the firmware.
fiesta_hardware_header(CLOCKS_HW_HEADER "${CLOCKS_SRC}" "${HAL_LIB_EXTERNAL}")
fiesta_hal_include_dirs(HAL_MANAGED_INCLUDE_DIRS "${HAL_LIB_EXTERNAL}")
fiesta_hal_command_sources(HAL_COMMAND_SOURCES "${HAL_SRC}")
fiesta_hal_helper_sources(HAL_HELPER_SOURCES "${HAL_SRC}")

if(EXISTS ${HAL_LIB_EXTERNAL}/unity.c)
    set(UNITY_SOURCE ${HAL_LIB_EXTERNAL}/unity.c)
    set(UNITY_INCLUDE_DIR ${HAL_LIB_EXTERNAL})
elseif(EXISTS ${HAL_LIB_EXTERNAL}/src/utils/unity.c)
    set(UNITY_SOURCE ${HAL_LIB_EXTERNAL}/src/utils/unity.c)
    set(UNITY_INCLUDE_DIR ${HAL_LIB_EXTERNAL}/src/utils)
else()
    message(FATAL_ERROR "JaszczurHAL not found. Expected unity.c at:
  ${HAL_LIB_EXTERNAL}/unity.c
  ${HAL_LIB_EXTERNAL}/src/utils/unity.c")
endif()

add_library(clocks_mock STATIC
    ${CLOCKS_SRC}/buzzerStrategy.cpp
    ${CLOCKS_SRC}/clocksState.cpp
    ${CLOCKS_SRC}/config.cpp
    ${CLOCKS_SRC}/gps.cpp
    ${HAL_SRC}/hal/core/hal_assert.cpp
    ${HAL_SRC}/hal/core/hal_config.cpp
    ${HAL_SRC}/hal/core/jh_handle_pool.cpp
    ${HAL_HELPER_SOURCES}
    ${HAL_COMMAND_SOURCES}
    ${HAL_SRC}/hal/security/hal_sc_auth.cpp
    ${HAL_SRC}/hal/serial/hal_serial.cpp
    ${HAL_SRC}/hal/serial/hal_serial_commands.cpp
    ${HAL_SRC}/hal/serial/hal_serial_session.cpp
    ${HAL_SRC}/hal/debug/hal_debug_format.cpp
    ${HAL_SRC}/hal/security/jh_secure_random.cpp
    ${HAL_SRC}/hal/security/hal_crypto.cpp
    ${HAL_SRC}/hal/security/jh_sha256.c
    # HAL crypto wrappers call into shared WireGuard crypto primitives.
    ${HAL_SRC}/hal/network/wireguard/core/crypto/chacha20.c
    ${HAL_SRC}/hal/network/wireguard/core/crypto/chacha20poly1305.c
    ${HAL_SRC}/hal/network/wireguard/core/crypto/crypto.c
    ${HAL_SRC}/hal/network/wireguard/core/crypto/poly1305-donna.c
    ${HAL_SRC}/hal/impl/.mock/hal_wifi.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_system.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_sync.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_serial.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_secure_random.cpp
    ${HAL_SRC}/hal/gps/hal_gps.cpp
    ${HAL_SRC}/hal/gps/hal_gps_core.cpp
    ${HAL_SRC}/hal/gps/hal_gps_nmea_utils.cpp
    ${HAL_SRC}/hal/gps/gps_nmea_parser.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gps.cpp
    ${HAL_SRC}/hal/time/hal_time.cpp
    ${HAL_SRC}/hal/time/jh_calendar.c
    ${UNITY_SOURCE}
    # Shared SC parameter and router service implementations. The native
    # firmware source bridge includes the same files once.
    ${SCDEFS}/sc_param_handlers.c
    ${SCDEFS}/sc_command_handlers.c
)

target_include_directories(clocks_mock BEFORE PUBLIC
    ${CLOCKS_SRC}/tests/include   # test-only hal_project_config.h override
)
fiesta_include_hardware_header("${CLOCKS_HW_HEADER}" clocks_mock)

target_include_directories(clocks_mock PUBLIC
    ${HAL_MANAGED_INCLUDE_DIRS}
    ${CLOCKS_SRC}
    ${UNITY_INCLUDE_DIR}
    ${HAL_LIB_EXTERNAL}
    ${HAL_SRC}
    ${HAL_SRC}/utils
    ${CANDEFS}
    ${SCDEFS}
)

target_compile_definitions(clocks_mock PUBLIC
    UNIT_TEST
    UNITY_INCLUDE_CONFIG_H
    SERIAL_7N1=2
    SERIAL_8N1=6
)

target_compile_options(clocks_mock PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
)

add_subdirectory(tests)

# Optional analysis helpers kept outside the default build.
fiesta_add_host_analysis_targets(Clocks CLOCKS
    CLANG_TIDY_FILES "^.*/src/(Clocks|common/scDefinitions|common/tests)/.*\\.(c|cpp)$")
