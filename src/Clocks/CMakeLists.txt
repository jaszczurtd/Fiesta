cmake_minimum_required(VERSION 3.16)
project(Clocks_Tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

enable_testing()

set(CLOCKS_SRC ${CMAKE_CURRENT_SOURCE_DIR})
set(HAL_LIB_SUBMODULE ${CMAKE_CURRENT_SOURCE_DIR}/../library/JaszczurHAL)
set(HAL_LIB_EXTERNAL ${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/JaszczurHAL)

if(EXISTS ${HAL_LIB_SUBMODULE}/unity.c)
    set(UNITY_SOURCE ${HAL_LIB_SUBMODULE}/unity.c)
    set(UNITY_INCLUDE_DIR ${HAL_LIB_SUBMODULE})
elseif(EXISTS ${HAL_LIB_SUBMODULE}/src/utils/unity.c)
    set(UNITY_SOURCE ${HAL_LIB_SUBMODULE}/src/utils/unity.c)
    set(UNITY_INCLUDE_DIR ${HAL_LIB_SUBMODULE}/src/utils)
elseif(EXISTS ${HAL_LIB_EXTERNAL}/unity.c)
    set(UNITY_SOURCE ${HAL_LIB_EXTERNAL}/unity.c)
    set(UNITY_INCLUDE_DIR ${HAL_LIB_EXTERNAL})
elseif(EXISTS ${HAL_LIB_EXTERNAL}/src/utils/unity.c)
    set(UNITY_SOURCE ${HAL_LIB_EXTERNAL}/src/utils/unity.c)
    set(UNITY_INCLUDE_DIR ${HAL_LIB_EXTERNAL}/src/utils)
else()
    message(FATAL_ERROR "JaszczurHAL not found. Expected unity.c at:
  ${HAL_LIB_SUBMODULE}/unity.c
  ${HAL_LIB_SUBMODULE}/src/utils/unity.c
  ${HAL_LIB_EXTERNAL}/unity.c
  ${HAL_LIB_EXTERNAL}/src/utils/unity.c")
endif()

add_library(clocks_mock STATIC
    ${CLOCKS_SRC}/buzzerStrategy.cpp
    ${UNITY_SOURCE}
)

target_include_directories(clocks_mock PUBLIC
    ${CLOCKS_SRC}
    ${UNITY_INCLUDE_DIR}
)

target_compile_definitions(clocks_mock PUBLIC
    UNITY_INCLUDE_CONFIG_H
)

target_compile_options(clocks_mock PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
)

add_subdirectory(tests)
