# Helper: create a test executable linked against ecu_mock
function(add_ecu_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE ecu_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_ecu_test(test_glowPlugs)
add_ecu_test(test_ecu_params)
set_tests_properties(test_ecu_params PROPERTIES
    FAIL_REGULAR_EXPRESSION "GPS resume queued: HAL_NONE")
add_ecu_test(test_ecuPersistence)
add_ecu_test(test_sc_protocol)
add_ecu_test(test_sc_command_handlers)
add_ecu_test(test_engineFan)
add_ecu_test(test_engineHeater)
add_ecu_test(test_engine_operation)
add_ecu_test(test_heatedWindshield)
add_ecu_test(test_sensors_calc)
add_ecu_test(test_obd2)
add_ecu_test(test_can)
add_ecu_test(test_hal_wrappers)
add_ecu_test(test_engineFuel)
add_ecu_test(test_dtcManager)
add_ecu_test(test_dtc_recovery)
add_ecu_test(test_turbo)
add_ecu_test(test_engine_maps)
add_ecu_test(test_adjustometer)
add_ecu_test(test_vp37)
add_ecu_test(test_vp37_current)
add_executable(test_vp37_current_fast test_vp37_current.cpp ../vp37_current.c)
target_compile_definitions(test_vp37_current_fast PRIVATE VP37_PWM_FREQUENCY_HZ=1000)
target_link_libraries(test_vp37_current_fast PRIVATE ecu_mock)
add_test(NAME test_vp37_current_fast COMMAND test_vp37_current_fast)
add_executable(test_vp37_current_200hz test_vp37_current.cpp ../vp37_current.c)
target_compile_definitions(test_vp37_current_200hz PRIVATE VP37_PWM_FREQUENCY_HZ=200)
target_link_libraries(test_vp37_current_200hz PRIVATE ecu_mock)
add_test(NAME test_vp37_current_200hz COMMAND test_vp37_current_200hz)
# The host library is built without functional tests, which is how the ECU
# ships. These two binaries compile the same sources with the tests enabled:
# one repeats the VP37 suite against the fixtures, the other covers the
# registry, the sequencer and the console parser.
# Every unit of the VP37 module, so the fixture binaries never mix units
# built with the functional tests with archive members built without.
set(ECU_VP37_SOURCES ../vp37.c ../vp37_feedback.c ../vp37_compensation.c
    ../vp37_control.c ../vp37_telemetry.c)
set(ECU_TEST_FIXTURE_SOURCES ${ECU_VP37_SOURCES} ../tests.c ../test_helpers.c)
add_executable(test_vp37_bench test_vp37.cpp ${ECU_TEST_FIXTURE_SOURCES})
target_compile_definitions(test_vp37_bench PRIVATE ECU_FUNCTIONAL_TESTS_ENABLED=1)
target_link_libraries(test_vp37_bench PRIVATE ecu_mock)
add_test(NAME test_vp37_bench COMMAND test_vp37_bench)
add_executable(test_tests_registry test_tests_registry.cpp
    ${ECU_TEST_FIXTURE_SOURCES})
target_compile_definitions(test_tests_registry
    PRIVATE ECU_FUNCTIONAL_TESTS_ENABLED=1)
target_link_libraries(test_tests_registry PRIVATE ecu_mock)
# The release at zero demand is a build option; this binary compiles the VP37
# units with it off and checks that the bottom is held under drive instead.
add_executable(test_vp37_hold_at_min test_vp37_hold_at_min.cpp
    ${ECU_VP37_SOURCES})
target_compile_definitions(test_vp37_hold_at_min
    PRIVATE VP37_PWM_DISABLE_AT_MIN_POSITION=0)
target_link_libraries(test_vp37_hold_at_min PRIVATE ecu_mock)
add_test(NAME test_vp37_hold_at_min COMMAND test_vp37_hold_at_min)
add_test(NAME test_tests_registry COMMAND test_tests_registry)
add_ecu_test(test_rpm)

if(UNIX)
    add_test(NAME test_bootstrap_dependencies
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/test_bootstrap_dependencies.sh")
endif()
