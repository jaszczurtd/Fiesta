# Reproduce memcheck failures: a passing program with a definite leak and a
# program that fails a check with a clean Valgrind report. The leak's
# allocation stack must reach the build log, also after a cached configure;
# the log must end with a summary that names the failed test and repeats its
# failure line, and must leave out the reports of passing tests.
cmake_minimum_required(VERSION 3.16)
file(MAKE_DIRECTORY "${OUTPUT_DIR}/source")
file(WRITE "${OUTPUT_DIR}/source/fixture.c" [=[
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *volatile allocation;
int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "leak") == 0) {
        allocation = malloc(17);
        if (allocation == NULL) return 2;
        allocation = NULL;
    }
    if (argc > 1 && strcmp(argv[1], "fail") == 0) {
        puts("fixture.c:20:test_before:PASS");
        puts("fixture.c:21:test_step:FAIL: Expected [81; Was 80");
        puts("fixture.c:22:test_reports_Error:PASS");
        return 1;
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
add_test(NAME fixture COMMAND fixture ${FIXTURE_MODE})
add_test(NAME passing_fixture COMMAND fixture passing)
add_test(NAME excluded_fixture COMMAND fixture leak)
set_tests_properties(excluded_fixture PROPERTIES LABELS excluded)
fiesta_add_host_analysis_targets(fixture FIXTURE
    CLANG_TIDY_FILES "fixture.c$" MEMCHECK_EXCLUDE_LABELS excluded)
]=])
string(CONFIGURE "${project}" project @ONLY)
file(WRITE "${OUTPUT_DIR}/source/CMakeLists.txt" "${project}")

set(temporary "${OUTPUT_DIR}/build/Testing/Temporary")
foreach(mode IN ITEMS pass leak fail pass)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        -S "${OUTPUT_DIR}/source" -B "${OUTPUT_DIR}/build" -G "${GENERATOR}"
        "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DFIXTURE_MODE=${mode}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Fixture configure failed: ${output}\n${error}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${OUTPUT_DIR}/build"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Fixture build failed: ${output}\n${error}")
    endif()
    # A report or failed test list of an earlier run must neither appear in
    # the output nor survive this run.
    file(MAKE_DIRECTORY "${temporary}")
    file(WRITE "${temporary}/MemoryChecker.999.log" "STALE_MEMCHECK_REPORT")
    file(WRITE "${temporary}/LastTestsFailed_00000000-0000.log"
        "1:STALE_FAILED_TEST\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${OUTPUT_DIR}/build"
        --target check-valgrind
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    set(output "${output}\n${error}")
    if(output MATCHES "STALE_MEMCHECK_REPORT|STALE_FAILED_TEST" OR
       EXISTS "${temporary}/MemoryChecker.999.log" OR
       EXISTS "${temporary}/LastTestsFailed_00000000-0000.log")
        message(FATAL_ERROR "Memcheck retained an earlier report: ${output}")
    endif()
    if(mode STREQUAL "pass")
        if(NOT result EQUAL 0 OR
           output MATCHES "Valgrind report:|Memcheck summary")
            message(FATAL_ERROR
                "Clean fixture failed or printed failure reports: ${output}")
        endif()
        continue()
    endif()

    # The summary comes last: no report and no test output after it.
    string(FIND "${output}" "Memcheck summary" summary_at)
    if(result EQUAL 0 OR summary_at EQUAL -1 OR
       NOT output MATCHES "CTest memcheck failed:")
        message(FATAL_ERROR "Missing failure or summary: ${output}")
    endif()
    string(SUBSTRING "${output}" ${summary_at} -1 summary)
    if(summary MATCHES "Valgrind report:|:PASS" OR
       output MATCHES "fixture passing")
        message(FATAL_ERROR
            "Summary not last or a passing test reported: ${output}")
    endif()
    if(mode STREQUAL "leak")
        string(FIND "${summary}"
            "\n  #1 fixture: Valgrind found errors, report above" named)
        if(named EQUAL -1 OR NOT output MATCHES "definitely lost" OR
           NOT output MATCHES "Valgrind report:")
            message(FATAL_ERROR "Missing allocation report: ${output}")
        endif()
    else()
        # The clean report of a failed check stays out; the failure line,
        # brackets and semicolon included, stands in the summary.
        string(FIND "${summary}" "\n  #1 fixture: the test failed, Valgrind found no errors\n      fixture.c:21:test_step:FAIL: Expected [81; Was 80\n"
            named)
        if(named EQUAL -1 OR output MATCHES "Valgrind report:")
            message(FATAL_ERROR
                "Missing failure line or clean report printed: ${output}")
        endif()
    endif()
endforeach()
