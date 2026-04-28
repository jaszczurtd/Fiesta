cmake_minimum_required(VERSION 3.16)
project(Adjustometer_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

enable_testing()

set(ADJ_SRC  ${CMAKE_CURRENT_SOURCE_DIR})
set(HAL_LIB  ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/JaszczurHAL)
set(HAL_SRC  ${HAL_LIB}/src)
set(HAL_STUBS ${HAL_SRC}/arduino_host_stubs)
set(CANDEFS ${CMAKE_CURRENT_SOURCE_DIR}/../common/canDefinitions)

# ── Mock implementations ───────────────────────────────────────────────────────
file(GLOB MOCK_SOURCES ${HAL_SRC}/hal/impl/.mock/*.cpp)

# ── HAL utility sources compilable on host ─────────────────────────────────────
set(HAL_UTIL_SOURCES
    ${HAL_SRC}/hal/hal_config.cpp
    ${HAL_SRC}/hal/hal_can_util.cpp
    ${HAL_SRC}/hal/hal_kv.cpp
    ${HAL_SRC}/hal/hal_soft_timer.cpp
    ${HAL_SRC}/utils/SmartTimers.cpp
    ${HAL_SRC}/utils/pidController.cpp
    ${HAL_SRC}/utils/unity.c
)

# ── Adjustometer sources under test ────────────────────────────────────────────
set(ADJ_SOURCES
    ${ADJ_SRC}/sensors.c
    ${ADJ_SRC}/led.c
)

# Compile .c sources as C++ so __atomic_* builtins and hal C++ APIs work
set_source_files_properties(${ADJ_SOURCES} PROPERTIES LANGUAGE CXX)

find_package(Threads REQUIRED)

# ── adj_mock library ──────────────────────────────────────────────────────────
add_library(adj_mock STATIC
    ${MOCK_SOURCES}
    ${HAL_UTIL_SOURCES}
    ${ADJ_SOURCES}
    # tools.cpp needs Arduino.h / SD.h / SPI.h stubs
    ${HAL_SRC}/utils/tools.cpp
)

target_include_directories(adj_mock BEFORE PUBLIC
    ${ADJ_SRC}/tests/include   # test-only hal_project_config.h override
    ${HAL_STUBS}
)
target_include_directories(adj_mock PUBLIC
    ${HAL_SRC}
    ${HAL_SRC}/utils
    ${ADJ_SRC}
    ${CANDEFS}
)

target_compile_definitions(adj_mock PUBLIC
    UNITY_INCLUDE_CONFIG_H
)

target_link_libraries(adj_mock PUBLIC Threads::Threads)

target_compile_options(adj_mock PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_library(adj_mock_common STATIC
    ${MOCK_SOURCES}
    ${HAL_UTIL_SOURCES}
    ${HAL_SRC}/utils/tools.cpp
)

target_include_directories(adj_mock_common BEFORE PUBLIC
    ${ADJ_SRC}/tests/include
    ${HAL_STUBS}
)
target_include_directories(adj_mock_common PUBLIC
    ${HAL_SRC}
    ${HAL_SRC}/utils
    ${ADJ_SRC}
    ${CANDEFS}
)

target_compile_definitions(adj_mock_common PUBLIC
    UNITY_INCLUDE_CONFIG_H
)

target_link_libraries(adj_mock_common PUBLIC Threads::Threads)

target_compile_options(adj_mock_common PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ── Tests ─────────────────────────────────────────────────────────────────────
add_subdirectory(tests)
