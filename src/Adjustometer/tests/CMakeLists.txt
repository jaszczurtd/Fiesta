function(add_adj_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE adj_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

function(add_adj_internal_test NAME)
    add_executable(${NAME} ${NAME}.cpp sensors_internal_bridge.cpp)
    target_link_libraries(${NAME} PRIVATE adj_mock_common)
    target_compile_definitions(${NAME} PRIVATE UNITY_INCLUDE_CONFIG_H)
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_adj_test(test_sensors)
add_adj_test(test_led)
add_adj_internal_test(test_sensors_internal)
