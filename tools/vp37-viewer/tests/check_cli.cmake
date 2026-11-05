cmake_minimum_required(VERSION 3.22)

file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${VIEWER}" build WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result STREQUAL "2" OR NOT error MATCHES "directories explicitly")
  message(FATAL_ERROR "Missing paths were not rejected: ${result}\n${output}\n${error}")
endif()
if(EXISTS "${WORK}/site" OR EXISTS "${WORK}/cache")
  message(FATAL_ERROR "The command wrote output without explicit directories")
endif()
execute_process(COMMAND "${VIEWER}" parse "${FIXTURES}/logs/rev148-install.log.gz"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result STREQUAL "0" OR NOT output MATCHES "revision 148")
  message(FATAL_ERROR "Single-log parse failed: ${result}\n${output}\n${error}")
endif()

foreach(arguments IN ITEMS "serve" "serve;--log" "serve;--log;x;--port;65536"
    "serve;--log;x;--port;8037x" "serve;--log;x;--address;0.0.0.0")
  execute_process(COMMAND "${VIEWER}" ${arguments}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result STREQUAL "1" OR NOT error MATCHES "serve:")
    message(FATAL_ERROR "Invalid serve arguments accepted: ${arguments}\n${output}\n${error}")
  endif()
endforeach()
