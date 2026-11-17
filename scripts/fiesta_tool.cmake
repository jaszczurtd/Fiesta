# Prepare the local C registry tool. The only stdout line is its absolute path.
cmake_minimum_required(VERSION 3.22)
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(hal_args "")
if(DEFINED ENV{FIESTA_HAL_ROOT} AND NOT "$ENV{FIESTA_HAL_ROOT}" STREQUAL "")
    set(hal_args "-DFIESTA_HAL_ROOT=$ENV{FIESTA_HAL_ROOT}"
        "-DJHBASE_ROOT=$ENV{FIESTA_HAL_ROOT}/third_party/jhbase")
else()
    set(hal_args "-DFIESTA_HAL_ROOT=${root}/src/JaszczurHAL"
        "-DJHBASE_ROOT=${root}/src/JaszczurHAL/third_party/jhbase")
endif()
if(FIESTA_TOOL_MEMCHECK)
    set(FIESTA_TOOL_TESTS ON)
elseif(NOT DEFINED FIESTA_TOOL_TESTS)
    set(FIESTA_TOOL_TESTS OFF)
endif()
if(NOT DEFINED FIESTA_TOOL_BUILD_DIR)
    set(FIESTA_TOOL_BUILD_DIR "${root}/.build/tools/modules")
endif()
file(MAKE_DIRECTORY "${FIESTA_TOOL_BUILD_DIR}")
file(LOCK "${FIESTA_TOOL_BUILD_DIR}/bootstrap.lock" GUARD PROCESS TIMEOUT 180
     RESULT_VARIABLE locked)
if(NOT locked STREQUAL "0")
    message(FATAL_ERROR "Cannot lock the Fiesta module tool: ${locked}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${root}/tools/modules"
    -B "${FIESTA_TOOL_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
    "-DBUILD_TESTING=${FIESTA_TOOL_TESTS}" ${hal_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot configure the Fiesta module tool:\n${output}${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${FIESTA_TOOL_BUILD_DIR}"
    --config Release --target fiesta-modules --parallel 4
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot build the Fiesta module tool:\n${output}${error}")
endif()
file(READ "${FIESTA_TOOL_BUILD_DIR}/tool-Release.txt" executable)
if(FIESTA_TOOL_TESTS)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${FIESTA_TOOL_BUILD_DIR}"
        --config Release --target fiesta_modules_tests --parallel 4
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot build registry tests:\n${output}${error}")
    endif()
    execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${FIESTA_TOOL_BUILD_DIR}"
        -C Release --output-on-failure
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Registry tests failed:\n${output}${error}")
    endif()
endif()
if(FIESTA_TOOL_MEMCHECK)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${FIESTA_TOOL_BUILD_DIR}"
        --config Release --target check-valgrind
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    message("${output}${error}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Registry memcheck failed: ${result}")
    endif()
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${executable}"
    COMMAND_ERROR_IS_FATAL ANY)
