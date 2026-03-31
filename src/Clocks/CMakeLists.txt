cmake_minimum_required(VERSION 3.16)
project(Clocks_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

enable_testing()

set(CLOCKS_SRC ${CMAKE_CURRENT_SOURCE_DIR})
set(HAL_LIB_SUBMODULE ${CMAKE_CURRENT_SOURCE_DIR}/../library/JaszczurHAL)
set(HAL_LIB_EXTERNAL ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/JaszczurHAL)

if(EXISTS ${HAL_LIB_SUBMODULE}/unity.c)
    set(HAL_LIB ${HAL_LIB_SUBMODULE})
elseif(EXISTS ${HAL_LIB_EXTERNAL}/unity.c)
    set(HAL_LIB ${HAL_LIB_EXTERNAL})
else()
    message(FATAL_ERROR "JaszczurHAL not found. Expected at ${HAL_LIB_SUBMODULE} or ${HAL_LIB_EXTERNAL}")
endif()

add_library(clocks_mock STATIC
    ${CLOCKS_SRC}/buzzerStrategy.cpp
    ${HAL_LIB}/unity.c
)

target_include_directories(clocks_mock PUBLIC
    ${CLOCKS_SRC}
    ${HAL_LIB}
)

target_compile_definitions(clocks_mock PUBLIC
    UNITY_INCLUDE_CONFIG_H
)

target_compile_options(clocks_mock PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
)

add_subdirectory(tests)
