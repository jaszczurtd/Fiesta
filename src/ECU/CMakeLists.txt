cmake_minimum_required(VERSION 3.16)
project(ECU_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# Valgrind / CTest memcheck configuration (copied from JaszczurHAL pattern).
# Host-only dynamic analysis for the unit-test suite (NOT firmware):
#   ctest --test-dir build_test -T memcheck --output-on-failure
find_program(MEMORYCHECK_COMMAND NAMES valgrind)
set(MEMORYCHECK_COMMAND_OPTIONS
    "--leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --track-origins=yes --error-exitcode=1"
    CACHE STRING "Flags passed to Valgrind by 'ctest -T memcheck'")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind.supp")
    set(MEMORYCHECK_SUPPRESSIONS_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind.supp"
    CACHE FILEPATH "Valgrind suppressions for known third-party/system noise" FORCE)
else()
    # Avoid stale cache values when the suppression file is removed.
    set(MEMORYCHECK_SUPPRESSIONS_FILE
        ""
        CACHE FILEPATH "Valgrind suppressions for known third-party/system noise" FORCE)
endif()

include(CTest)

# ── Paths ──────────────────────────────────────────────────────────────────────
set(HAL_LIB   ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/JaszczurHAL)
set(HAL_SRC   ${HAL_LIB}/src)
set(ECU_SRC   ${CMAKE_CURRENT_SOURCE_DIR})
set(CANDEFS   ${CMAKE_CURRENT_SOURCE_DIR}/../common/canDefinitions)
set(SCDEFS    ${CMAKE_CURRENT_SOURCE_DIR}/../common/scDefinitions)
set(ECU_STUBS ${ECU_SRC}/tests/stubs)

include(${HAL_LIB}/cmake/jh_managed_frameworks.cmake)
jh_managed_framework_include_dirs(HAL_MANAGED_INCLUDE_DIRS)

# ── Mock implementations (from JaszczurHAL) ────────────────────────────────────
file(GLOB MOCK_SOURCES ${HAL_SRC}/hal/impl/.mock/*.cpp)

# ── HAL utility sources used by the host build ─────────────────────────────────
set(HAL_UTIL_SOURCES
    ${HAL_SRC}/hal/core/hal_assert.cpp
    ${HAL_SRC}/hal/core/hal_config.cpp
    ${HAL_SRC}/hal/i2c/hal_i2c.cpp
    ${HAL_SRC}/hal/spi/hal_spi.cpp
    ${HAL_SRC}/hal/gps/hal_gps.cpp
    ${HAL_SRC}/hal/security/hal_sc_auth.cpp
    ${HAL_SRC}/hal/serial/hal_serial.cpp
    ${HAL_SRC}/hal/serial/hal_serial_session.cpp
    ${HAL_SRC}/hal/time/hal_time.cpp
    ${HAL_SRC}/hal/time/hal_time_ntp.cpp
    ${HAL_SRC}/hal/debug/hal_debug_format.cpp
    ${HAL_SRC}/hal/gps/gps_nmea_parser.cpp
    ${HAL_SRC}/hal/gps/hal_gps_core.cpp
    ${HAL_SRC}/hal/time/jh_calendar.c
    ${HAL_SRC}/hal/security/jh_secure_random.cpp
    ${HAL_SRC}/hal/security/hal_crypto.cpp
    # HAL crypto wrappers call into the shared WireGuard crypto primitives.
    ${HAL_SRC}/hal/network/wireguard/core/crypto/chacha20.c
    ${HAL_SRC}/hal/network/wireguard/core/crypto/chacha20poly1305.c
    ${HAL_SRC}/hal/network/wireguard/core/crypto/crypto.c
    ${HAL_SRC}/hal/network/wireguard/core/crypto/poly1305-donna.c
    ${HAL_SRC}/hal/can/mcp2515/hal_can_mcp2515_config.cpp
    ${HAL_SRC}/hal/can/hal_can_util.cpp
    ${HAL_SRC}/hal/storage/hal_eeprom.cpp
    ${HAL_SRC}/hal/storage/hal_kv.cpp
    ${HAL_SRC}/hal/temperature/hal_thermocouple.cpp
    ${HAL_SRC}/hal/timers/hal_soft_timer.cpp
    ${HAL_SRC}/hal/control/hal_pid_controller.cpp
    ${HAL_SRC}/hal/timers/smart_timers/SmartTimers.cpp
    ${HAL_SRC}/utils/pidController.cpp
    ${HAL_SRC}/utils/unity.c
)

# ── ECU sources compilable without the multicoreWatchdog runtime ───────────────
#
# start.c - excluded: uses the multicoreWatchdog runtime dependency
# tests.c - excluded: placeholder only (empty initTests / startTests)
#
set(ECU_SOURCES
    ${ECU_SRC}/rpm.c
    ${ECU_SRC}/sensors.c
    ${ECU_SRC}/glowPlugs.c
    ${ECU_SRC}/engineFan.c
    ${ECU_SRC}/engineHeater.c
    ${ECU_SRC}/engine_operation.c
    ${ECU_SRC}/engineFuel.c
    ${ECU_SRC}/heatedWindshield.c
    ${ECU_SRC}/obd-2_mapping.c
    ${ECU_SRC}/obd-2.c
    ${ECU_SRC}/dtcManager.c
    ${ECU_SRC}/engineMaps.c
    ${ECU_SRC}/config.c
    ${ECU_SRC}/gps.c
    ${ECU_SRC}/can.c
    ${ECU_SRC}/turbo.c
    ${ECU_SRC}/vp37.c
    # R1.2: descriptor-driven SC reply machinery shared with the host
    # SerialConfigurator. The native firmware build picks this up through
    # the sc_param_handlers_glue.c include shim, so firmware and host tests
    # both compile the same
    # source file exactly once.
    ${SCDEFS}/sc_param_handlers.c
)

# ── ecu_mock: hal mock + tools + ECU sources + stubs for excluded modules ──────
add_library(ecu_mock STATIC
    ${MOCK_SOURCES}
    ${HAL_UTIL_SOURCES}
    ${ECU_SOURCES}
    ${ECU_STUBS}/ecu_stubs.cpp
    ${HAL_SRC}/utils/tools.cpp
)

target_include_directories(ecu_mock BEFORE PUBLIC
    ${ECU_SRC}/tests/include   # test-only hal_project_config.h override
)
target_include_directories(ecu_mock PUBLIC
    ${HAL_MANAGED_INCLUDE_DIRS}
    ${HAL_SRC}
    ${HAL_SRC}/utils
    ${ECU_SRC}
    ${CANDEFS}
    ${SCDEFS}
)

target_compile_definitions(ecu_mock PUBLIC
    UNIT_TEST
    UNITY_INCLUDE_CONFIG_H
    HAL_TARGET_MOCK
    SERIAL_7N1=2
    SERIAL_8N1=6
)

find_package(Threads REQUIRED)
target_link_libraries(ecu_mock PUBLIC Threads::Threads)

target_compile_options(ecu_mock PRIVATE
    -Wall
    -Wextra
    -Werror
)

# Static-analysis integration.
# - check-cppcheck: manual target (cmake --build ... --target check-cppcheck)
# - test_cppcheck: regular CTest test (runs with ctest)
find_program(CPPCHECK_EXECUTABLE cppcheck)
option(ECU_ENABLE_CPPCHECK_TARGET "Enable check-cppcheck custom target" ON)

if(ECU_ENABLE_CPPCHECK_TARGET AND CPPCHECK_EXECUTABLE)
    file(GLOB ECU_CPPCHECK_SOURCES RELATIVE ${ECU_SRC} "${ECU_SRC}/*.c")

    set(ECU_CPPCHECK_ARGS
        --enable=all
        --std=c99
        -I.
        --suppress=missingInclude
        --suppress=missingIncludeSystem
        --suppress=unusedFunction
        --suppress=knownConditionTrueFalse
        --suppress=checkersReport
    )

    add_custom_target(check-cppcheck
        COMMAND ${CMAKE_COMMAND} -E chdir ${ECU_SRC}
            ${CPPCHECK_EXECUTABLE}
            ${ECU_CPPCHECK_ARGS}
            ${ECU_CPPCHECK_SOURCES}
        COMMENT "Running cppcheck for ECU sources"
        VERBATIM
    )

    add_test(
        NAME test_cppcheck
        COMMAND ${CPPCHECK_EXECUTABLE} ${ECU_CPPCHECK_ARGS} ${ECU_CPPCHECK_SOURCES}
    )
    set_tests_properties(test_cppcheck PROPERTIES
        WORKING_DIRECTORY ${ECU_SRC}
        LABELS "static-analysis"
    )
endif()

add_subdirectory(tests)

# Optional analysis helpers kept outside the default build.
option(ECU_ENABLE_VALGRIND_TARGET "Enable check-valgrind custom target" ON)
if(ECU_ENABLE_VALGRIND_TARGET AND MEMORYCHECK_COMMAND)
    add_custom_target(check-valgrind
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} -T memcheck --output-on-failure -LE static-analysis
        COMMENT "Running CTest memcheck (Valgrind) for ECU"
        VERBATIM
    )
endif()

find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy)
option(ECU_ENABLE_CLANG_TIDY_TARGET "Enable check-clang-tidy custom target" ON)
if(ECU_ENABLE_CLANG_TIDY_TARGET AND RUN_CLANG_TIDY_EXECUTABLE)
    add_custom_target(check-clang-tidy
        COMMAND ${RUN_CLANG_TIDY_EXECUTABLE}
            -p ${CMAKE_BINARY_DIR}
            -quiet
            -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
            "^.*/src/(ECU|common/scDefinitions)/.*\\.(c|cpp)$"
        COMMENT "Running clang-tidy for ECU host-compilable sources"
        VERBATIM
    )
endif()
