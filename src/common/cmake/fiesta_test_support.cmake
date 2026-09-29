include_guard(GLOBAL)

# Host-test helpers shared by module tests; never part of a firmware build.
set(FIESTA_COMMON_TESTS_DIR "${CMAKE_CURRENT_LIST_DIR}/../tests")

# Adds the SerialConfigurator session helpers to a module test target. The
# helpers call the session functions from the module's own config.h.
function(fiesta_add_sc_session_test_support TARGET)
    target_sources(${TARGET} PRIVATE
        "${FIESTA_COMMON_TESTS_DIR}/sc_session_test_support.cpp")
endfunction()
