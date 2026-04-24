function(add_clocks_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE clocks_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_clocks_test(test_buzzerStrategy)
add_clocks_test(test_config_session)

add_executable(test_can
    test_can.cpp
    ${CLOCKS_SRC}/can.cpp
    ${HAL_SRC}/hal/hal_can_util.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_can.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gpio.cpp
    ${HAL_SRC}/utils/tools.cpp
)
target_link_libraries(test_can PRIVATE clocks_mock)
# HAL_SRC BEFORE so <tools.h> resolves to the HAL aggregator that also
# re-exports multicoreWatchdog's watchdog_feed(), not to utils/tools.h
# (which is the Arduino-dependent C++ utility surface).
target_include_directories(test_can BEFORE PRIVATE
    ${HAL_SRC}
    ${HAL_SRC}/arduino_host_stubs
)
add_test(NAME test_can COMMAND test_can)
