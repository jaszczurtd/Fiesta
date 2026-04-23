function(add_oilspeed_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE oilspeed_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_oilspeed_test(test_config_session)
