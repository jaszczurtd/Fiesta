cmake_minimum_required(VERSION 3.16)
project(Clocks_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

enable_testing()

set(CLOCKS_SRC ${CMAKE_CURRENT_SOURCE_DIR})
set(HAL_LIB_EXTERNAL ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/JaszczurHAL)
set(HAL_SRC ${HAL_LIB_EXTERNAL}/src)
set(CANDEFS ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/canDefinitions)

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
    ${CLOCKS_SRC}/config.cpp
    ${HAL_SRC}/hal/hal_config.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_system.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_sync.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_serial.cpp
    ${UNITY_SOURCE}
)

target_include_directories(clocks_mock PUBLIC
    ${CLOCKS_SRC}
    ${UNITY_INCLUDE_DIR}
    ${HAL_LIB_EXTERNAL}
    ${HAL_SRC}
    ${HAL_SRC}/utils
    ${CANDEFS}
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
