function(add_adj_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE adj_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_adj_test(test_sensors)
add_adj_test(test_led)
