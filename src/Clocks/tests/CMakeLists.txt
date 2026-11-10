function(add_clocks_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE clocks_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_clocks_test(test_buzzerStrategy)
add_clocks_test(test_gps)

# The fuel bar, drawn on the HAL display mock.
add_executable(test_engineFuel
    test_engineFuel.cpp
    ${CLOCKS_SRC}/engineFuel.cpp
    ${HAL_SRC}/hal/display/hal_display.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_display.cpp
    ${HAL_SRC}/hal/time/hal_time.cpp
    ${HAL_SRC}/hal/time/jh_calendar.c
)
target_link_libraries(test_engineFuel PRIVATE clocks_mock)
add_test(NAME test_engineFuel COMMAND test_engineFuel)
add_clocks_test(test_config_session)
fiesta_add_sc_session_test_support(test_config_session)

# The cluster tests behind the configurator operations: the shared runner,
# the output substitution and the real gauge, lamp and buzzer outputs.
set(CLOCKS_TESTS_SOURCES
    ${CLOCKS_SRC}/clocksTests.cpp
    ${CLOCKS_SRC}/testOutputs.cpp
    ${CLOCKS_SRC}/clusterOutput.cpp
    ${CLOCKS_SRC}/Cluster.cpp
    ${CLOCKS_SRC}/chargeLamp.cpp
    ${CLOCKS_SRC}/oilLamp.cpp
    ${CLOCKS_SRC}/buzzer.cpp
    ${CLOCKS_SRC}/alerts.cpp
    ${TEST_RUNNER_SOURCES}
    ${HAL_SRC}/hal/timers/hal_timer.cpp
    ${HAL_SRC}/hal/timers/hal_timer_ext.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_timer.cpp
)

set(CLOCKS_CAN_TEST_SOURCES
    ${CLOCKS_SRC}/can.cpp
    ${HAL_SRC}/hal/can/mcp2515/hal_can_mcp2515_config.cpp
    ${HAL_SRC}/hal/can/hal_can.cpp
    ${HAL_SRC}/hal/can/hal_can_util.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_can.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gpio.cpp
    ${HAL_SRC}/hal/time/hal_time.cpp
    ${HAL_SRC}/hal/time/jh_calendar.c
    ${HAL_SRC}/hal/timers/hal_soft_timer.cpp
    ${HAL_SRC}/hal/timers/smart_timers/SmartTimers.cpp
)
add_executable(test_can test_can.cpp ${CLOCKS_CAN_TEST_SOURCES}
    ${CLOCKS_SRC}/chargeLamp.cpp ${CLOCKS_SRC}/testOutputs.cpp)
# Exercise ABS readiness through CAN publication and the real SC reply; the
# session offers the cluster tests, so their units come along.
target_sources(test_config_session PRIVATE ${CLOCKS_CAN_TEST_SOURCES}
    ${CLOCKS_TESTS_SOURCES})
target_include_directories(test_config_session BEFORE PRIVATE ${HAL_SRC})

# The five cluster tests: steps on the real outputs, release, CAN untouched.
add_executable(test_clocksTests test_clocksTests.cpp ${CLOCKS_CAN_TEST_SOURCES}
    ${CLOCKS_TESTS_SOURCES})
target_link_libraries(test_clocksTests PRIVATE clocks_mock)
target_include_directories(test_clocksTests BEFORE PRIVATE ${HAL_SRC})
add_test(NAME test_clocksTests COMMAND test_clocksTests)
target_link_libraries(test_can PRIVATE clocks_mock)
target_include_directories(test_can BEFORE PRIVATE ${HAL_SRC})
add_test(NAME test_can COMMAND test_can)

# ABS speed, moved with its existing algorithm unchanged.
add_executable(test_speed
    test_speed.cpp
    ${CLOCKS_SRC}/speed.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gpio.cpp
)
target_link_libraries(test_speed PRIVATE clocks_mock)
add_test(NAME test_speed COMMAND test_speed)

add_executable(test_oilPressure
    test_oilPressure.cpp
    ${CLOCKS_SRC}/oilPressure.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_adc.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gpio.cpp
)
target_link_libraries(test_oilPressure PRIVATE clocks_mock)
add_test(NAME test_oilPressure COMMAND test_oilPressure)

# The charge lamp state table and its GP26 output.
add_executable(test_chargeLamp
    test_chargeLamp.cpp
    ${CLOCKS_SRC}/chargeLamp.cpp
    ${CLOCKS_SRC}/testOutputs.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gpio.cpp
)
target_link_libraries(test_chargeLamp PRIVATE clocks_mock)
add_test(NAME test_chargeLamp COMMAND test_chargeLamp)

# The oil lamp from the oil pressure sender, and its GP11 output.
add_executable(test_oilLamp
    test_oilLamp.cpp
    ${CLOCKS_SRC}/oilLamp.cpp
    ${CLOCKS_SRC}/testOutputs.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gpio.cpp
)
target_link_libraries(test_oilLamp PRIVATE clocks_mock)
add_test(NAME test_oilLamp COMMAND test_oilLamp)

# The oil and boost pressure gauges, drawn on the HAL display mock.
add_executable(test_pressureGauge
    test_pressureGauge.cpp
    ${CLOCKS_SRC}/pressureGauge.cpp
    ${HAL_SRC}/hal/display/hal_display.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_display.cpp
    ${HAL_SRC}/hal/time/hal_time.cpp
    ${HAL_SRC}/hal/time/jh_calendar.c
)
target_link_libraries(test_pressureGauge PRIVATE clocks_mock)
add_test(NAME test_pressureGauge COMMAND test_pressureGauge)

# The small gauges under the big icons and the connection dots.
add_executable(test_simpleGauge
    test_simpleGauge.cpp
    ${CLOCKS_SRC}/simpleGauge.cpp
    ${HAL_SRC}/hal/display/hal_display.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_display.cpp
    ${HAL_SRC}/hal/time/hal_time.cpp
    ${HAL_SRC}/hal/time/jh_calendar.c
)
target_link_libraries(test_simpleGauge PRIVATE clocks_mock)
add_test(NAME test_simpleGauge COMMAND test_simpleGauge)

# Statistics of the TIMING variant, built with them on.
add_executable(test_timingDiag
    test_timingDiag.cpp
    ${CLOCKS_SRC}/timingDiag.cpp
)
target_link_libraries(test_timingDiag PRIVATE clocks_mock)
target_compile_definitions(test_timingDiag PRIVATE CLOCKS_TIMING_DIAG=1)
add_test(NAME test_timingDiag COMMAND test_timingDiag)

# The core-0 pass of logic.cpp under the TIMING statistics; everything else
# it calls is a stub in the test.
add_executable(test_core0Timing
    test_core0Timing.cpp
    ${CLOCKS_SRC}/logic.cpp
    ${CLOCKS_SRC}/timingDiag.cpp
    ${HAL_SRC}/hal/display/hal_display.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_display.cpp
    ${HAL_SRC}/hal/gpio/hal_rgb_led.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_rgb_led.cpp
    ${HAL_SRC}/hal/timers/hal_soft_timer.cpp
    ${HAL_SRC}/hal/timers/smart_timers/SmartTimers.cpp
    ${HAL_SRC}/hal/time/hal_time.cpp
    ${HAL_SRC}/hal/time/jh_calendar.c
)
target_link_libraries(test_core0Timing PRIVATE clocks_mock)
target_include_directories(test_core0Timing BEFORE PRIVATE ${HAL_SRC})
target_compile_definitions(test_core0Timing PRIVATE CLOCKS_TIMING_DIAG=1)
add_test(NAME test_core0Timing COMMAND test_core0Timing)

# Both MCP9600 behind a fake of the HAL thermocouple API.
add_executable(test_egt
    test_egt.cpp
    ${CLOCKS_SRC}/egt.cpp
)
target_link_libraries(test_egt PRIVATE clocks_mock)
add_test(NAME test_egt COMMAND test_egt)
