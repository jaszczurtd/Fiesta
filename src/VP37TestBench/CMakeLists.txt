cmake_minimum_required(VERSION 3.16)
project(VP37TestBench_Tests CXX C)

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
        CACHE FILEPATH "Valgrind suppressions for known third-party/system noise")
endif()

include(CTest)

set(BENCH_SRC ${CMAKE_CURRENT_SOURCE_DIR})
set(HAL_LIB   ${CMAKE_CURRENT_SOURCE_DIR}/../JaszczurHAL)
set(HAL_SRC   ${HAL_LIB}/src)

include(${HAL_LIB}/cmake/jh_managed_frameworks.cmake)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/fiesta_hal_helper_sources.cmake")
# The VP37 module: the list in src/common/vp37/vp37_sources.txt.
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/vp37/vp37_sources.cmake")
jh_managed_framework_include_dirs(HAL_MANAGED_INCLUDE_DIRS)
fiesta_hal_helper_sources(HAL_HELPER_SOURCES "${HAL_SRC}")

# ── Mock implementations ───────────────────────────────────────────────────────
file(GLOB MOCK_SOURCES ${HAL_SRC}/hal/impl/.mock/*.cpp)
# The bench has no GPS responsibility; keep its test graph free from the
# feature-specific mock injection translation unit as well.
list(FILTER MOCK_SOURCES EXCLUDE REGEX "/hal_gps\\.cpp$")

# ── HAL utility sources compilable on host ─────────────────────────────────────
set(HAL_UTIL_SOURCES
    ${HAL_SRC}/hal/analog/hal_adc_scan.cpp
    ${HAL_SRC}/hal/core/hal_assert.cpp
    ${HAL_SRC}/hal/core/hal_config.cpp
    ${HAL_SRC}/hal/core/jh_handle_pool.cpp
    ${HAL_HELPER_SOURCES}
    ${HAL_SRC}/hal/i2c/hal_i2c.cpp
    ${HAL_SRC}/hal/spi/hal_spi.cpp
    ${HAL_SRC}/hal/serial/hal_serial.cpp
    ${HAL_SRC}/hal/time/hal_time.cpp
    ${HAL_SRC}/hal/time/hal_time_ntp.cpp
    ${HAL_SRC}/hal/debug/hal_debug_format.cpp
    # The mock display backend leans on the shared display core.
    ${HAL_SRC}/hal/display/hal_display.cpp
    ${HAL_SRC}/hal/time/jh_calendar.c
    ${HAL_SRC}/hal/security/jh_secure_random.cpp
    ${HAL_SRC}/hal/security/hal_crc.cpp
    ${HAL_SRC}/hal/can/mcp2515/hal_can_mcp2515_config.cpp
    ${HAL_SRC}/hal/can/hal_can.cpp
    ${HAL_SRC}/hal/can/hal_can_util.cpp
    ${HAL_SRC}/hal/storage/hal_eeprom.cpp
    ${HAL_SRC}/hal/storage/hal_kv.cpp
    ${HAL_SRC}/hal/temperature/hal_thermocouple.cpp
    ${HAL_SRC}/hal/timers/hal_soft_timer.cpp
    ${HAL_SRC}/hal/timers/smart_timers/SmartTimers.cpp
    ${HAL_SRC}/hal/control/hal_pid_controller.cpp
    ${HAL_SRC}/utils/pidController.cpp
    ${HAL_SRC}/utils/unity.c
)

# ── Bench sources under test ───────────────────────────────────────────────────
# The module and the adapter are C translation units, compiled as C like in
# the firmware and in the ECU host build. The bench application (logic,
# peripherials) compiles too, so a defect there fails the host build, not
# only the firmware one; the multicoreWatchdog runtime it calls is stubbed
# in tests/stubs/bench_stubs.cpp.
set(BENCH_SOURCES
    ${VP37_SOURCES}
    ${BENCH_SRC}/vp37_bench_adapter.c
    ${BENCH_SRC}/logic.cpp
    ${BENCH_SRC}/peripherials.cpp
)

find_package(Threads REQUIRED)

# ── bench_mock library ────────────────────────────────────────────────────────
add_library(bench_mock STATIC
    ${MOCK_SOURCES}
    ${HAL_UTIL_SOURCES}
    ${BENCH_SOURCES}
    ${BENCH_SRC}/tests/stubs/bench_stubs.cpp
)

target_include_directories(bench_mock BEFORE PUBLIC
    ${BENCH_SRC}/tests/include   # test-only hal_project_config.h override
)
target_include_directories(bench_mock PUBLIC
    ${HAL_MANAGED_INCLUDE_DIRS}
    ${HAL_SRC}
    ${HAL_SRC}/utils
    ${BENCH_SRC}
    ${VP37_SOURCE_DIR}
)

target_compile_definitions(bench_mock PUBLIC
    UNIT_TEST
    UNITY_INCLUDE_CONFIG_H
)

target_link_libraries(bench_mock PUBLIC Threads::Threads)

target_compile_options(bench_mock PRIVATE -Wall -Wextra -Werror)

# ── Tests ─────────────────────────────────────────────────────────────────────
add_subdirectory(tests)

# Optional analysis helpers kept outside the default build.
option(VP37TESTBENCH_ENABLE_VALGRIND_TARGET "Enable check-valgrind custom target" ON)
if(VP37TESTBENCH_ENABLE_VALGRIND_TARGET AND MEMORYCHECK_COMMAND)
    add_custom_target(check-valgrind
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} -T memcheck --output-on-failure
        COMMENT "Running CTest memcheck (Valgrind) for VP37TestBench"
        VERBATIM
    )
endif()

find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy)
option(VP37TESTBENCH_ENABLE_CLANG_TIDY_TARGET "Enable check-clang-tidy custom target" ON)
if(VP37TESTBENCH_ENABLE_CLANG_TIDY_TARGET AND RUN_CLANG_TIDY_EXECUTABLE)
    add_custom_target(check-clang-tidy
        COMMAND ${RUN_CLANG_TIDY_EXECUTABLE}
            -p ${CMAKE_BINARY_DIR}
            -quiet
            -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
            "^.*/src/VP37TestBench/.*\\.(c|cpp)$"
        COMMENT "Running clang-tidy for VP37TestBench host-compilable sources"
        VERBATIM
    )
endif()
