# CTest sends Valgrind's allocation stacks to separate files. Include those
# files in failed build logs so local runs and CI expose the same diagnosis.
if(NOT DEFINED CTEST_COMMAND OR NOT IS_DIRECTORY "${BUILD_DIR}")
    message(FATAL_ERROR "CTEST_COMMAND and an existing BUILD_DIR are required")
endif()

set(log_pattern "${BUILD_DIR}/Testing/Temporary/MemoryChecker.*.log")
file(GLOB old_logs "${log_pattern}")
if(old_logs)
    file(REMOVE ${old_logs})
endif()

set(exclude_labels "")
if(NOT "${EXCLUDE_LABELS}" STREQUAL "")
    set(exclude_labels -LE "${EXCLUDE_LABELS}")
endif()
execute_process(
    COMMAND "${CTEST_COMMAND}" --test-dir "${BUILD_DIR}" -T memcheck
        --output-on-failure ${exclude_labels}
    WORKING_DIRECTORY "${BUILD_DIR}"
    RESULT_VARIABLE result)

if(NOT "${result}" STREQUAL "0")
    file(GLOB logs "${log_pattern}")
    foreach(log IN LISTS logs)
        file(READ "${log}" content)
        message("Valgrind report: ${log}\n${content}")
    endforeach()
    message(FATAL_ERROR "CTest memcheck failed: ${result}")
endif()
