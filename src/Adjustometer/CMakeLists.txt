cmake_minimum_required(VERSION 3.16)
project(Adjustometer_Tests CXX C)

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

# Optional analysis helpers kept outside the default build.
option(ADJUSTOMETER_ENABLE_VALGRIND_TARGET "Enable check-valgrind custom target" ON)
if(ADJUSTOMETER_ENABLE_VALGRIND_TARGET AND MEMORYCHECK_COMMAND)
    add_custom_target(check-valgrind
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} -T memcheck --output-on-failure
        COMMENT "Running CTest memcheck (Valgrind) for Adjustometer"
        VERBATIM
    )
endif()

find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy)
option(ADJUSTOMETER_ENABLE_CLANG_TIDY_TARGET "Enable check-clang-tidy custom target" ON)
if(ADJUSTOMETER_ENABLE_CLANG_TIDY_TARGET AND RUN_CLANG_TIDY_EXECUTABLE)
    add_custom_target(check-clang-tidy
        COMMAND ${RUN_CLANG_TIDY_EXECUTABLE}
            -p ${CMAKE_BINARY_DIR}
            -quiet
            -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
            "^.*/src/Adjustometer/.*\\.(c|cpp)$"
        COMMENT "Running clang-tidy for Adjustometer host-compilable sources"
        VERBATIM
    )
endif()
