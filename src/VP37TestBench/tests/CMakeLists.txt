function(add_bench_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE bench_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_bench_test(test_bench_adapter)
add_bench_test(test_bench_logic)
