# The C harness obtains the real HTTP catalog and emits browser assertions.
file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${FIXTURE}" --browser-fixture "${WORK}/test.html"
  RESULT_VARIABLE generated TIMEOUT 20)
if(NOT generated STREQUAL "0")
  message(FATAL_ERROR "Browser fixture failed: ${generated}")
endif()
string(REPLACE " " "%20" page "${WORK}/test.html")
if(WIN32)
  set(page "file:///${page}")
else()
  set(page "file://${page}")
endif()
file(REMOVE_RECURSE "${WORK}/profile")
execute_process(COMMAND "${BROWSER}" --headless --disable-gpu --disable-extensions
  --no-first-run --disable-background-networking --disable-component-update
  --js-flags=--max-old-space-size=128 "--user-data-dir=${WORK}/profile"
  --dump-dom --timeout=30000 --virtual-time-budget=2000 "${page}"
  OUTPUT_FILE "${WORK}/result.html" ERROR_FILE "${WORK}/browser.log"
  RESULT_VARIABLE result TIMEOUT 60)
file(READ "${WORK}/result.html" html)
if(NOT result STREQUAL "0" OR NOT html MATCHES "<pre id=\"result\">PASS [0-9]+</pre>")
  string(REGEX MATCH "<pre id=\"result\">[^<]*" failure "${html}")
  message(FATAL_ERROR "Browser failed (${result}): ${failure}; see ${WORK}/browser.log")
endif()
string(REGEX MATCH "<pre id=\"result\">PASS [0-9]+" summary "${html}")
message(STATUS "${summary}")
file(REMOVE_RECURSE "${WORK}/profile")
