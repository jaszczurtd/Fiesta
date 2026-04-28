cmake_minimum_required(VERSION 3.16)
project(ECU_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

enable_testing()

# ── Paths ──────────────────────────────────────────────────────────────────────
set(HAL_SRC   ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/JaszczurHAL/src)
set(ECU_SRC   ${CMAKE_CURRENT_SOURCE_DIR})
set(CANDEFS   ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/canDefinitions)
set(SCDEFS    ${CMAKE_CURRENT_SOURCE_DIR}/../common/scDefinitions)
set(HAL_STUBS ${HAL_SRC}/arduino_host_stubs)
set(ECU_STUBS ${ECU_SRC}/tests/stubs)

# ── Mock implementations (from JaszczurHAL) ────────────────────────────────────
file(GLOB MOCK_SOURCES ${HAL_SRC}/hal/impl/.mock/*.cpp)

# ── HAL utility sources (no Arduino dependency) ────────────────────────────────
set(HAL_UTIL_SOURCES
    ${HAL_SRC}/hal/hal_config.cpp
    ${HAL_SRC}/hal/hal_crypto.cpp
    ${HAL_SRC}/hal/hal_can_util.cpp
    ${HAL_SRC}/hal/hal_kv.cpp
    ${HAL_SRC}/hal/hal_soft_timer.cpp
    ${HAL_SRC}/hal/hal_pid_controller.cpp
    ${HAL_SRC}/utils/SmartTimers.cpp
    ${HAL_SRC}/utils/pidController.cpp
    ${HAL_SRC}/utils/unity.c
)

# ── ECU sources compilable without arduino-timer / multicoreWatchdog ───────────
#
# rpm.c   - excluded: uses arduino-timer (Timer class)
# start.c - excluded: uses multicoreWatchdog (which uses arduino-timer)
# tests.c - excluded: placeholder only (empty initTests / startTests)
#
set(ECU_SOURCES
    ${ECU_SRC}/sensors.c
    ${ECU_SRC}/glowPlugs.c
    ${ECU_SRC}/engineFan.c
    ${ECU_SRC}/engineHeater.c
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
    # SerialConfigurator. The arduino-cli build picks this up through
    # the sc_param_handlers_glue.c include shim instead, so the
    # firmware sketch and host tests both end up compiling the same
    # source file exactly once.
    ${SCDEFS}/sc_param_handlers.c
)

# ── ecu_mock: hal mock + tools + ECU sources + stubs for excluded modules ──────
add_library(ecu_mock STATIC
    ${MOCK_SOURCES}
    ${HAL_UTIL_SOURCES}
    ${ECU_SOURCES}
    ${ECU_STUBS}/ecu_stubs.cpp
    # tools.cpp requires arduino_host_stubs so <Arduino.h>/<SD.h>/<SPI.h> resolve
    ${HAL_SRC}/utils/tools.cpp
)

# ECU_STUBS first: provides arduino-timer.h stub that shadows the real library
# HAL_STUBS second: provides Arduino.h / SD.h / SPI.h stubs for tools.cpp
target_include_directories(ecu_mock BEFORE PUBLIC
    ${ECU_STUBS}
    ${HAL_STUBS}
)
target_include_directories(ecu_mock PUBLIC
    ${HAL_SRC}
    ${HAL_SRC}/utils
    ${ECU_SRC}
    ${CANDEFS}
    ${SCDEFS}
)

target_compile_definitions(ecu_mock PUBLIC
    UNIT_TEST
    UNITY_INCLUDE_CONFIG_H
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
    set_tests_properties(test_cppcheck PROPERTIES WORKING_DIRECTORY ${ECU_SRC})
endif()

add_subdirectory(tests)
