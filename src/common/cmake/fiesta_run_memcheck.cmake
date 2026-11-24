# Runs CTest memcheck over a module build. CTest sends Valgrind's reports to
# separate files; after a failure this script prints the reports that show an
# error, with their allocation stacks, and ends with a summary of the failed
# tests and the failure lines of their output. Unattended runs mail only the
# end of the log, so the cause has to stand there and not among clean reports.
if(NOT DEFINED CTEST_COMMAND OR NOT IS_DIRECTORY "${BUILD_DIR}")
    message(FATAL_ERROR "CTEST_COMMAND and an existing BUILD_DIR are required")
endif()

# Lines of a failed test's output that the summary repeats, at most
# failure_line_limit per test.
set(failure_pattern
    "FAIL|Fail|ERROR|Error|Assert|Abort|Segmentation fault|Traceback|not ok")
set(failure_line_limit 12)

# Reports, failed test lists and test logs of an earlier run.
set(temporary "${BUILD_DIR}/Testing/Temporary")
file(GLOB old_logs
    "${temporary}/MemoryChecker.*.log"
    "${temporary}/LastTestsFailed_*.log"
    "${temporary}/LastDynamicAnalysis_*.log")
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
if("${result}" STREQUAL "0")
    return()
endif()

# CTest lists a failed test as "<number>:<name>"; the number also names its
# MemoryChecker.<number>.log.
set(last 0)
file(GLOB failed_lists "${temporary}/LastTestsFailed_*.log")
foreach(failed_list IN LISTS failed_lists)
    file(STRINGS "${failed_list}" entries REGEX "^[0-9]+:")
    foreach(entry IN LISTS entries)
        string(REGEX MATCH "^([0-9]+):(.*)$" matched "${entry}")
        set(failed_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
        if(CMAKE_MATCH_1 GREATER last)
            set(last "${CMAKE_MATCH_1}")
        endif()
    endforeach()
endforeach()
file(GLOB reports "${temporary}/MemoryChecker.*.log")
foreach(report IN LISTS reports)
    if(report MATCHES "MemoryChecker\\.([0-9]+)\\.log$")
        if(CMAKE_MATCH_1 GREATER last)
            set(last "${CMAKE_MATCH_1}")
        endif()
    endif()
endforeach()

# The output of every test. Brackets and semicolons in it would split or join
# the list of matched lines, so control characters stand in for them until
# the lines are printed.
set(analysis "")
file(GLOB analysis_logs "${temporary}/LastDynamicAnalysis_*.log")
foreach(analysis_log IN LISTS analysis_logs)
    file(READ "${analysis_log}" text)
    string(APPEND analysis "${text}")
endforeach()
string(ASCII 1 open_bracket)
string(ASCII 2 close_bracket)
string(ASCII 3 semicolon)
string(REPLACE "[" "${open_bracket}" analysis "${analysis}")
string(REPLACE "]" "${close_bracket}" analysis "${analysis}")
string(REPLACE ";" "${semicolon}" analysis "${analysis}")

# Sets out to the failure lines of the named test's output, indented, or to
# an empty string.
function(failure_lines name out)
    set(lines "")
    string(REPLACE "[" "${open_bracket}" name "${name}")
    string(REPLACE "]" "${close_bracket}" name "${name}")
    string(FIND "${analysis}" "\"${name}\" start time:" start)
    if(start GREATER -1)
        string(SUBSTRING "${analysis}" ${start} -1 output)
        string(FIND "${output}" "\nOutput:\n" begin)
        string(FIND "${output}" "\n<end of output>" end)
        if(begin GREATER -1 AND end GREATER begin)
            math(EXPR length "${end} - ${begin}")
            string(SUBSTRING "${output}" ${begin} ${length} output)
            string(REGEX MATCHALL "[^\n]*(${failure_pattern})[^\n]*"
                lines "${output}")
            # A passing Unity test whose name contains one of the words.
            list(FILTER lines EXCLUDE REGEX ":PASS$")
        endif()
    endif()
    list(LENGTH lines count)
    if(count EQUAL 0)
        set(${out} "" PARENT_SCOPE)
        return()
    endif()
    set(more "")
    if(count GREATER failure_line_limit)
        list(SUBLIST lines 0 ${failure_line_limit} lines)
        math(EXPR count "${count} - ${failure_line_limit}")
        set(more "\n      ... ${count} more in the test output above")
    endif()
    list(JOIN lines "\n      " text)
    string(REPLACE "${open_bracket}" "[" text "${text}")
    string(REPLACE "${close_bracket}" "]" text "${text}")
    string(REPLACE "${semicolon}" ";" text "${text}")
    set(${out} "      ${text}${more}" PARENT_SCOPE)
endfunction()

# A report shows a problem when Valgrind counted an error; for a failed test
# also when the process died on a signal or the report ends without Valgrind's
# summary, as after a timeout.
set(summary "")
if(last GREATER 0)
    foreach(index RANGE 1 ${last})
        set(report "${temporary}/MemoryChecker.${index}.log")
        set(content "")
        set(problem "")
        if(EXISTS "${report}")
            file(READ "${report}" content)
            if(content MATCHES "ERROR SUMMARY: [1-9]")
                set(problem "Valgrind found errors")
            elseif(DEFINED failed_${index} AND
                   content MATCHES "Process terminating")
                set(problem "the process died on a signal")
            elseif(DEFINED failed_${index} AND
                   NOT content MATCHES "ERROR SUMMARY:")
                set(problem "the Valgrind report ends early")
            endif()
        endif()
        if(NOT "${problem}" STREQUAL "")
            message("Valgrind report: ${report}\n${content}")
            set(problem "${problem}, report above")
        endif()
        if(DEFINED failed_${index})
            if(NOT "${problem}" STREQUAL "")
                set(state "${problem}")
            elseif(EXISTS "${report}")
                set(state "the test failed, Valgrind found no errors")
            else()
                set(state "the test failed without a Valgrind report")
            endif()
            failure_lines("${failed_${index}}" lines)
            if("${lines}" STREQUAL "" AND "${problem}" STREQUAL "")
                set(lines "      (no failure line in the test output above)")
            endif()
            string(APPEND summary "\n  #${index} ${failed_${index}}: ${state}")
            if(NOT "${lines}" STREQUAL "")
                string(APPEND summary "\n${lines}")
            endif()
        elseif(NOT "${problem}" STREQUAL "")
            set(command "")
            if(content MATCHES "Command: ([^\n]*)")
                set(command ": ${CMAKE_MATCH_1}")
            endif()
            string(APPEND summary
                "\n  #${index} passed, but ${problem}${command}")
        endif()
    endforeach()
endif()
if("${summary}" STREQUAL "")
    set(summary "\n  CTest reported no failed test, see its output above")
endif()
message("Memcheck summary (reports in ${temporary}):${summary}")
message(FATAL_ERROR "CTest memcheck failed: ${result}")
