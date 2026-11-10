# Reproduce a passing program whose definite leak makes memcheck fail.
# Its allocation stack must reach the build log, also after a cached configure.
cmake_minimum_required(VERSION 3.16)
file(MAKE_DIRECTORY "${OUTPUT_DIR}/source")
file(WRITE "${OUTPUT_DIR}/source/fixture.c" [=[
#include <stdlib.h>
static void *volatile allocation;
int main(int argc, char **argv) {
    (void)argv;
    if (argc > 1) {
        allocation = malloc(17);
        if (allocation == NULL) return 2;
        allocation = NULL;
    }
    return 0;
}
]=])
set(project [=[
cmake_minimum_required(VERSION 3.16)
project(memcheck_report_fixture C)
include("@SOURCE_ROOT@/src/common/cmake/fiesta_host_analysis.cmake")
include(CTest)
add_executable(fixture fixture.c)
set(args "")
if(FIXTURE_LEAK)
    set(args leak)
endif()
add_test(NAME fixture COMMAND fixture ${args})
add_test(NAME excluded_fixture COMMAND fixture leak)
set_tests_properties(excluded_fixture PROPERTIES LABELS excluded)
fiesta_add_host_analysis_targets(fixture FIXTURE
    CLANG_TIDY_FILES "fixture.c$" MEMCHECK_EXCLUDE_LABELS excluded)
]=])
string(CONFIGURE "${project}" project @ONLY)
file(WRITE "${OUTPUT_DIR}/source/CMakeLists.txt" "${project}")

foreach(leak IN ITEMS OFF ON OFF)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        -S "${OUTPUT_DIR}/source" -B "${OUTPUT_DIR}/build" -G "${GENERATOR}"
        "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DFIXTURE_LEAK=${leak}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Fixture configure failed: ${output}\n${error}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${OUTPUT_DIR}/build"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Fixture build failed: ${output}\n${error}")
    endif()
    # An old report must neither appear in the output nor survive this run.
    file(MAKE_DIRECTORY "${OUTPUT_DIR}/build/Testing/Temporary")
    file(WRITE "${OUTPUT_DIR}/build/Testing/Temporary/MemoryChecker.999.log"
        "STALE_MEMCHECK_REPORT")
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${OUTPUT_DIR}/build"
        --target check-valgrind
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    set(output "${output}\n${error}")
    if(output MATCHES "STALE_MEMCHECK_REPORT" OR
       EXISTS "${OUTPUT_DIR}/build/Testing/Temporary/MemoryChecker.999.log")
        message(FATAL_ERROR "Memcheck retained an earlier report: ${output}")
    endif()
    if(leak)
        if(result EQUAL 0 OR NOT output MATCHES "definitely lost" OR
           NOT output MATCHES "Valgrind report:" OR
           NOT output MATCHES "CTest memcheck failed:")
            message(FATAL_ERROR "Missing failure or allocation report: ${output}")
        endif()
    elseif(NOT result EQUAL 0 OR output MATCHES "Valgrind report:")
        message(FATAL_ERROR "Clean fixture failed or printed failure reports: ${output}")
    endif()
endforeach()
