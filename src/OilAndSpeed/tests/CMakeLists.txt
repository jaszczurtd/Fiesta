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
