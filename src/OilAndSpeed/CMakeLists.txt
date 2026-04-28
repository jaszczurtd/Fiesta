cmake_minimum_required(VERSION 3.16)
project(OilAndSpeed_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

enable_testing()

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
    ${HAL_SRC}/hal/hal_crypto.cpp
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
