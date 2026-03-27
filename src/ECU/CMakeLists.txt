cmake_minimum_required(VERSION 3.16)
project(ECU_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

enable_testing()

# ── Paths ──────────────────────────────────────────────────────────────────────
set(HAL_SRC   ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/JaszczurHAL/src)
set(ECU_SRC   ${CMAKE_CURRENT_SOURCE_DIR})
set(CANDEFS   ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/canDefinitions)
set(HAL_STUBS ${HAL_SRC}/arduino_host_stubs)
set(ECU_STUBS ${ECU_SRC}/tests/stubs)

# ── Mock implementations (from JaszczurHAL) ────────────────────────────────────
file(GLOB MOCK_SOURCES ${HAL_SRC}/hal/impl/.mock/*.cpp)

# ── HAL utility sources (no Arduino dependency) ────────────────────────────────
set(HAL_UTIL_SOURCES
    ${HAL_SRC}/hal/hal_config.cpp
    ${HAL_SRC}/SmartTimers.cpp
    ${HAL_SRC}/pidController.cpp
    ${HAL_SRC}/unity.c
)

# ── ECU sources compilable without arduino-timer / multicoreWatchdog ───────────
#
# rpm.cpp   — excluded: uses arduino-timer (Timer class)
# start.cpp — excluded: uses multicoreWatchdog (which uses arduino-timer)
# tests.cpp — excluded: placeholder only (empty initTests / startTests)
#
set(ECU_SOURCES
    ${ECU_SRC}/sensors.cpp
    ${ECU_SRC}/glowPlugs.cpp
    ${ECU_SRC}/engineFan.cpp
    ${ECU_SRC}/engineHeater.cpp
    ${ECU_SRC}/engineFuel.cpp
    ${ECU_SRC}/heatedWindshield.cpp
    ${ECU_SRC}/obd-2_mapping.cpp
    ${ECU_SRC}/obd-2.cpp
    ${ECU_SRC}/dtcManager.cpp
    ${ECU_SRC}/engineMaps.cpp
    ${ECU_SRC}/config.cpp
    ${ECU_SRC}/gps.cpp
    ${ECU_SRC}/can.cpp
    ${ECU_SRC}/turbo.cpp
    ${ECU_SRC}/vp37.cpp
)

# ── ecu_mock: hal mock + tools + ECU sources + stubs for excluded modules ──────
add_library(ecu_mock STATIC
    ${MOCK_SOURCES}
    ${HAL_UTIL_SOURCES}
    ${ECU_SOURCES}
    ${ECU_STUBS}/ecu_stubs.cpp
    # tools.cpp requires arduino_host_stubs so <Arduino.h>/<SD.h>/<SPI.h> resolve
    ${HAL_SRC}/tools.cpp
)

# ECU_STUBS first: provides arduino-timer.h stub that shadows the real library
# HAL_STUBS second: provides Arduino.h / SD.h / SPI.h stubs for tools.cpp
target_include_directories(ecu_mock BEFORE PUBLIC
    ${ECU_STUBS}
    ${HAL_STUBS}
)
target_include_directories(ecu_mock PUBLIC
    ${HAL_SRC}
    ${ECU_SRC}
    ${CANDEFS}
)

target_compile_definitions(ecu_mock PUBLIC
    UNITY_INCLUDE_CONFIG_H
    SERIAL_7N1=2
    SERIAL_8N1=6
)

find_package(Threads REQUIRED)
target_link_libraries(ecu_mock PUBLIC Threads::Threads)

target_compile_options(ecu_mock PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
)

add_subdirectory(tests)
