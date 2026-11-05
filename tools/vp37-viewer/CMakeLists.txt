cmake_minimum_required(VERSION 3.22)
project(vp37_measurement_viewer VERSION 0.1.0 LANGUAGES C CXX)

# Use the HAL revision pinned by Fiesta for JSON, files, SHA-256 and inflate.
get_filename_component(_vp37v_default_hal "${CMAKE_CURRENT_SOURCE_DIR}/../../src/JaszczurHAL" ABSOLUTE)
set(VP37V_HAL_ROOT "${_vp37v_default_hal}" CACHE PATH "JaszczurHAL checkout")
if(NOT EXISTS "${VP37V_HAL_ROOT}/cmake/jh_jhbase.cmake")
  message(FATAL_ERROR "JaszczurHAL not found at ${VP37V_HAL_ROOT}; set VP37V_HAL_ROOT")
endif()

include("${VP37V_HAL_ROOT}/cmake/jh_jhbase.cmake")
set(JHBASE_HOST ON CACHE BOOL "Build host file operations" FORCE)
jh_add_jhbase()

include("${VP37V_HAL_ROOT}/cmake/jh_host_network.cmake")
jh_add_host_network_library(vp37v_network)
target_compile_definitions(vp37v_network PUBLIC
  HAL_WEBSOCKET_MAX_PAYLOAD_SIZE=32768
  HAL_WEBSOCKET_MAX_CLIENTS=4
  HAL_HTTP_SERVER_MAX_CLIENTS=4
  HAL_TCP_SOCKET_MAX_INSTANCES=16
  HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE=524288
  HAL_HTTP_FILES_IO_BUFFER_SIZE=4096
  HAL_HTTP_FILES_PATH_MAX=4096)

set(_vp37v_crypto "${VP37V_HAL_ROOT}/src/hal/network/wireguard/core/crypto")
add_library(vp37v_hal STATIC
  "${VP37V_HAL_ROOT}/src/hal/security/hal_crypto.cpp"
  "${VP37V_HAL_ROOT}/src/hal/security/jh_sha256.c"
  "${VP37V_HAL_ROOT}/src/hal/core/hal_text.cpp"
  "${_vp37v_crypto}/chacha20.c" "${_vp37v_crypto}/chacha20poly1305.c"
  "${_vp37v_crypto}/crypto.c" "${_vp37v_crypto}/poly1305-donna.c"
  "${VP37V_HAL_ROOT}/third_party/lodepng/lodepng.cpp")
target_include_directories(vp37v_hal PUBLIC "${VP37V_HAL_ROOT}/src" "${VP37V_HAL_ROOT}/third_party/lodepng")
target_compile_definitions(vp37v_hal PUBLIC HAL_ENABLE_CRYPTO
  LODEPNG_NO_COMPILE_ENCODER LODEPNG_NO_COMPILE_DISK)
target_link_libraries(vp37v_hal PUBLIC jb::status)

add_library(vp37v_core STATIC
  src/builder.cpp src/export.cpp src/json_io.cpp src/log_source.cpp src/metrics_view.cpp
  src/recording_cache.cpp src/run_index.cpp src/runs_model.cpp src/site.cpp
  src/telemetry.cpp src/telemetry_fields.cpp src/telemetry_stream.c)
target_include_directories(vp37v_core PUBLIC src)
target_link_libraries(vp37v_core PUBLIC jb::host PRIVATE vp37v_hal)
add_library(vp37v_file STATIC src/live_file.cpp)
target_link_libraries(vp37v_file PRIVATE jb::status)
add_library(vp37v_live STATIC src/live_server.cpp)
target_link_libraries(vp37v_live PUBLIC vp37v_core vp37v_network PRIVATE vp37v_file)
if(MSVC)
  # jhbase exports /OPT:REF, which is incompatible with incremental linking.
  target_link_options(vp37v_core INTERFACE /INCREMENTAL:NO)
  # HAL keeps two responses on the stack when rejecting a malformed request.
  target_link_options(vp37v_live INTERFACE /STACK:2097152)
endif()

add_executable(vp37-viewer src/main.cpp)
target_link_libraries(vp37-viewer PRIVATE vp37v_live)
target_compile_definitions(vp37-viewer PRIVATE VP37V_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/web")

foreach(target vp37v_hal vp37v_core vp37v_file vp37v_live vp37-viewer)
  set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
endforeach()
foreach(target vp37v_core vp37v_file vp37v_live vp37-viewer)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow)
  endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
  add_executable(vp37v_live_tests tests/test_live.cpp)
  target_link_libraries(vp37v_live_tests PRIVATE vp37v_live)
  target_compile_definitions(vp37v_live_tests PRIVATE
    VP37V_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    VP37V_WEB="${CMAKE_CURRENT_SOURCE_DIR}/web"
    VP37V_LIVE_WORK="${CMAKE_CURRENT_BINARY_DIR}/live-work")
  if(MSVC)
    target_compile_options(vp37v_live_tests PRIVATE /W4 /WX /utf-8 /EHsc)
  else()
    target_compile_options(vp37v_live_tests PRIVATE -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow)
  endif()
  add_test(NAME vp37v_live_tests COMMAND vp37v_live_tests)
  set_tests_properties(vp37v_live_tests PROPERTIES TIMEOUT 120)
  add_executable(vp37v_tests tests/test_viewer.cpp)
  target_link_libraries(vp37v_tests PRIVATE vp37v_core vp37v_hal)
  set_target_properties(vp37v_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_compile_definitions(vp37v_tests PRIVATE
    VP37V_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    VP37V_WEB="${CMAKE_CURRENT_SOURCE_DIR}/web"
    VP37V_WORK="${CMAKE_CURRENT_BINARY_DIR}/test-work")
  if(MSVC)
    target_compile_options(vp37v_tests PRIVATE /W4 /WX /utf-8 /EHsc)
  else()
    target_compile_options(vp37v_tests PRIVATE -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow)
  endif()
  add_test(NAME vp37v_tests COMMAND vp37v_tests)
  add_test(NAME vp37v_cli COMMAND "${CMAKE_COMMAND}"
    "-DVIEWER=$<TARGET_FILE:vp37-viewer>"
    "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-work"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.cmake")
  set_tests_properties(vp37v_cli PROPERTIES LABELS "tooling")
endif()
