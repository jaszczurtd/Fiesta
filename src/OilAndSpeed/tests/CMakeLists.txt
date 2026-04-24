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
target_include_directories(test_speed BEFORE PRIVATE
    ${HAL_SRC}/arduino_host_stubs
)
target_compile_definitions(test_speed PRIVATE
    PI=3.14159265358979323846
)
add_test(NAME test_speed COMMAND test_speed)

add_executable(test_can
    test_can.cpp
    ${OILSPEED_SRC}/can.cpp
    ${HAL_SRC}/hal/hal_can_util.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_can.cpp
    ${HAL_SRC}/hal/impl/.mock/hal_gpio.cpp
    ${HAL_SRC}/utils/tools.cpp
)
target_link_libraries(test_can PRIVATE oilspeed_mock)
# HAL_SRC BEFORE so <tools.h> resolves to the HAL aggregator that also
# re-exports multicoreWatchdog's watchdog_feed(), not to utils/tools.h
# (which is the Arduino-dependent C++ utility surface).
target_include_directories(test_can BEFORE PRIVATE
    ${HAL_SRC}
    ${HAL_SRC}/arduino_host_stubs
)
add_test(NAME test_can COMMAND test_can)
