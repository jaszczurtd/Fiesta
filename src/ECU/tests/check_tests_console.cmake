cmake_minimum_required(VERSION 3.16)
# Capture the actual debug transport, including line endings. The existing
# registry cases exercise ECU start/stop/sequence; bench tools depend on these
# exact records, in addition to the state assertions in that executable.
execute_process(COMMAND "${REGISTRY_TEST}" RESULT_VARIABLE status
    OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Registry test failed: ${status}\n${output}\n${errors}")
endif()
foreach(record
        "TEST: manual started"
        "TEST: manual stopped"
        "TEST: manual result: stopped"
        "TEST: none running"
        "TEST: sequence started"
        "TEST: sequence finished"
        "TEST: watch 2000 ms"
        "TEST: watch off"
        "TEST: manual result: host_lost")
    string(FIND "${output}" "${record}\n" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing exact console record: ${record}")
    endif()
endforeach()
