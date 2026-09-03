function(add_oilspeed_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE oilspeed_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_oilspeed_test(test_config_session)

add_executable(test_speed
    test_speed.cpp
    ${OILSPEED_SRC}/speed.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gpio.cpp
)
target_link_libraries(test_speed PRIVATE oilspeed_mock)
target_compile_definitions(test_speed PRIVATE
    PI=3.14159265358979323846
)
add_test(NAME test_speed COMMAND test_speed)

add_executable(test_can
    test_can.cpp
    ${OILSPEED_SRC}/can.cpp
    ${HAL_SRC}/hal/can/mcp2515/hal_can_mcp2515_config.cpp
    ${HAL_SRC}/hal/can/hal_can_util.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_can.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gpio.cpp
    ${HAL_SRC}/hal/time/hal_time.cpp
    ${HAL_SRC}/hal/time/jh_calendar.c
)
target_link_libraries(test_can PRIVATE oilspeed_mock)
target_include_directories(test_can BEFORE PRIVATE
    ${HAL_SRC}
)
add_test(NAME test_can COMMAND test_can)
