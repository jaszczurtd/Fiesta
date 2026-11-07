cmake_minimum_required(VERSION 3.16)

# Isolate the real runner from hardware generation and managed dependencies.
set(root "${WORK_DIR}/misra input fixture")
set(ecu "${root}/src/ECU")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${ecu}/misra" "${root}/src/common/vp37"
    "${root}/src/common/test_runner" "${root}/src/Adjustometer/misra")
configure_file("${MISRA_SCRIPT}" "${ecu}/misra/check_misra.sh" COPYONLY)
file(WRITE "${ecu}/misra/suppressions.txt" "# No fixture suppressions\n")
file(WRITE "${ecu}/module.c" "/* Clean module */\n")
file(WRITE "${root}/src/common/vp37/probe with spaces.c" "/* misra-probe-error */\n")
file(WRITE "${root}/src/common/test_runner/runner.c" "/* misra-probe-error */\n")
file(WRITE "${root}/src/Adjustometer/adj.c" "/* misra-probe-error */\n")
file(WRITE "${root}/src/Adjustometer/misra/suppressions.txt" "# None\n")

function(scan expected output)
    execute_process(COMMAND bash "${ecu}/misra/check_misra.sh"
        --cppcheck "${CPPCHECK_STUB}" --out "${root}/${output}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status STREQUAL "${expected}")
        message(FATAL_ERROR "Expected exit ${expected}, got ${status}:\n${stdout}\n${stderr}")
    endif()
endfunction()

# Either shared directory can contain a finding that must stop the gate.
scan(1 vp37 --fail-paths "^\\.\\./common/vp37/")
scan(1 runner --fail-paths "^\\.\\./common/test_runner/")
file(READ "${root}/runner/active-findings.txt" findings)
foreach(path "../common/vp37/probe with spaces.c" "../common/test_runner/runner.c")
    string(FIND "${findings}" "${path}:1:1:" found)
    if(found LESS 0)
        message(FATAL_ERROR "Shared input was not scanned: ${path}")
    endif()
endforeach()

# An explicit module selection must not pull in ECU's shared directories.
scan(1 selected --project "${root}/src/Adjustometer" --fail-on-findings)
file(READ "${root}/selected/active-findings.txt" findings)
if(NOT findings STREQUAL "adj.c:1:1: style: fixture finding [misra-c2012-15.5]\n")
    message(FATAL_ERROR "Unexpected selected-module findings: ${findings}")
endif()

# A missing shared source directory is an input error, never a clean scan.
file(REMOVE "${root}/src/common/test_runner/runner.c")
scan(2 missing)
file(REMOVE_RECURSE "${root}")
