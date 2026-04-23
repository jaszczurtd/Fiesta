function(add_clocks_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE clocks_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_clocks_test(test_buzzerStrategy)
add_clocks_test(test_config_session)
