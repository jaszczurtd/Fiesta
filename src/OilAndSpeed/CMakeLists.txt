cmake_minimum_required(VERSION 3.16)
project(OilAndSpeed_Tests CXX C)

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

set(OILSPEED_SRC ${CMAKE_CURRENT_SOURCE_DIR})
set(OILSPEED_TEST_STUBS ${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs)
set(HAL_LIB_EXTERNAL ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/JaszczurHAL)
set(HAL_SRC ${HAL_LIB_EXTERNAL}/src)
set(CANDEFS ${CMAKE_CURRENT_SOURCE_DIR}/../common/canDefinitions)
set(SCDEFS  ${CMAKE_CURRENT_SOURCE_DIR}/../common/scDefinitions)

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

add_library(oilspeed_mock STATIC
    ${OILSPEED_SRC}/config.cpp
    ${HAL_SRC}/hal/hal_config.cpp
    ${HAL_SRC}/hal/impl/shared/compat/debug_format/hal_debug_format.cpp
    ${HAL_SRC}/hal/hal_crypto.cpp
    # HAL crypto wrappers call into shared WireGuard crypto primitives.
    ${HAL_SRC}/hal/impl/shared/frameworks/wireguard/crypto/chacha20.c
    ${HAL_SRC}/hal/impl/shared/frameworks/wireguard/crypto/chacha20poly1305.c
    ${HAL_SRC}/hal/impl/shared/frameworks/wireguard/crypto/crypto.c
    ${HAL_SRC}/hal/impl/shared/frameworks/wireguard/crypto/poly1305-donna.c
    ${HAL_SRC}/hal/impl/.mock/hal_wifi.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_system.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_sync.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_serial.cpp
    ${UNITY_SOURCE}
    # R1.5: descriptor-driven SC reply machinery shared with the host
    # SerialConfigurator. Firmware arduino-cli build picks this up via
    # sc_param_handlers_glue.c instead, so each source TU compiles
    # exactly once on either path.
    ${SCDEFS}/sc_param_handlers.c
)

target_include_directories(oilspeed_mock BEFORE PUBLIC
    ${OILSPEED_SRC}/tests/include   # test-only hal_project_config.h override
    ${OILSPEED_TEST_STUBS}
)

target_include_directories(oilspeed_mock PUBLIC
    ${OILSPEED_SRC}
    ${UNITY_INCLUDE_DIR}
    ${HAL_LIB_EXTERNAL}
    ${HAL_SRC}
    ${HAL_SRC}/utils
    ${CANDEFS}
    ${SCDEFS}
)

target_compile_definitions(oilspeed_mock PUBLIC
    UNIT_TEST
    UNITY_INCLUDE_CONFIG_H
    SERIAL_7N1=2
    SERIAL_8N1=6
)

target_compile_options(oilspeed_mock PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
)

add_subdirectory(tests)

# Optional analysis helpers kept outside the default build.
option(OILANDSPEED_ENABLE_VALGRIND_TARGET "Enable check-valgrind custom target" ON)
if(OILANDSPEED_ENABLE_VALGRIND_TARGET AND MEMORYCHECK_COMMAND)
    add_custom_target(check-valgrind
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} -T memcheck --output-on-failure
        COMMENT "Running CTest memcheck (Valgrind) for OilAndSpeed"
        VERBATIM
    )
endif()

find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy)
option(OILANDSPEED_ENABLE_CLANG_TIDY_TARGET "Enable check-clang-tidy custom target" ON)
if(OILANDSPEED_ENABLE_CLANG_TIDY_TARGET AND RUN_CLANG_TIDY_EXECUTABLE)
    add_custom_target(check-clang-tidy
        COMMAND ${RUN_CLANG_TIDY_EXECUTABLE}
            -p ${CMAKE_BINARY_DIR}
            -quiet
            -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
            "^.*/src/(OilAndSpeed|common/scDefinitions)/.*\\.(c|cpp)$"
        COMMENT "Running clang-tidy for OilAndSpeed host-compilable sources"
        VERBATIM
    )
endif()
