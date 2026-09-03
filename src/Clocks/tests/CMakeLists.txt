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
    ${HAL_SRC}/hal/can/mcp2515/hal_can_mcp2515_config.cpp
    ${HAL_SRC}/hal/can/hal_can_util.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_can.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gpio.cpp
    ${HAL_SRC}/hal/time/hal_time.cpp
    ${HAL_SRC}/hal/time/jh_calendar.c
)
target_link_libraries(test_can PRIVATE clocks_mock)
target_include_directories(test_can BEFORE PRIVATE
    ${HAL_SRC}
)
add_test(NAME test_can COMMAND test_can)
