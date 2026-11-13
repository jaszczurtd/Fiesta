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

# The production launcher reads the project entry and passes arguments without a shell.
file(TO_CMAKE_PATH "${MONITOR}" entry)
string(RANDOM LENGTH 4 ALPHABET 123456789 random)
math(EXPR port "20000 + ${random}")
math(EXPR websocket_port "${port} + 1")
foreach(mode IN ITEMS good fail overflow missing)
  set(project "${WORK}/monitor ${mode} space")
  file(MAKE_DIRECTORY "${project}/.vscode")
  set(launcher "${entry}")
  if(WIN32)
    set(launcher "${project}/monitor entry.cmd")
    file(WRITE "${launcher}" "@\"${MONITOR}\" %*\r\n")
  endif()
  if(mode STREQUAL "missing")
    set(launcher "${project}/missing entry")
  endif()
  file(WRITE "${project}/.vscode/settings.json"
    "{\"jaszczurhal.vscodeEntry\":\"${launcher}\",\"jaszczurhal.vscodeEntryWindows\":\"${launcher}\"}")
  set(log "${project}/capture.log")
  file(WRITE "${log}" "previous partial record")
  execute_process(COMMAND "${VIEWER}" serve --log "${log}" --monitor-project "${project}"
    --port "${port}" --ws-port "${websocket_port}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
  if(mode STREQUAL "good")
    if(NOT result STREQUAL "0")
      message(FATAL_ERROR "Monitor capture failed: ${result}\n${output}\n${error}")
    endif()
    file(WRITE "${project}/terminal.txt" "${output}")
    execute_process(COMMAND "${MONITOR}" --check-monitor-help
      "${project}/terminal.txt" "${log}" "${port}"
      RESULT_VARIABLE help_result ERROR_VARIABLE help_error)
    if(NOT help_result STREQUAL "0")
      message(FATAL_ERROR "Monitor help failed: ${help_result}\n${help_error}\n${output}")
    endif()
    file(READ "${log}" content)
    if(NOT content MATCHES "^previous partial record\n[0-9]+\\.[0-9]+ HOST MONITOR Live capture started\n"
        OR NOT content MATCHES "ECU: VP37 C us:1000" OR NOT content MATCHES "ECU: VP37 C us:2000"
        OR content MATCHES "ECU: ECU:" OR content MATCHES "partial$")
      message(FATAL_ERROR "Invalid capture:\n${content}")
    endif()
    execute_process(COMMAND "${VIEWER}" parse "${log}"
      RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result STREQUAL "0" OR NOT output MATCHES "C rows 2 keys")
      message(FATAL_ERROR "Captured data did not parse: ${result}\n${output}\n${error}")
    endif()
  elseif(NOT result STREQUAL "1")
    message(FATAL_ERROR "Broken monitor accepted (${mode}): ${result}\n${output}\n${error}")
  endif()
endforeach()
