function(add_adj_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE adj_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

function(add_adj_internal_test NAME)
    set(test_source "${NAME}.cpp")
    if(ARGC GREATER 1)
        set(test_source "${ARGV1}")
    endif()
    add_executable(${NAME} ${test_source} sensors_internal_bridge.cpp)
    target_link_libraries(${NAME} PRIVATE adj_mock_common)
    target_compile_definitions(${NAME} PRIVATE UNITY_INCLUDE_CONFIG_H)
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_adj_test(test_sensors)
add_adj_test(test_led)
add_adj_test(test_telemetry)
add_adj_internal_test(test_sensors_internal)
target_compile_definitions(test_sensors_internal PRIVATE
    ADJUSTOMETER_SLIDING_WINDOW=0 ADJUSTOMETER_PWM_FILTER_US=0)
add_adj_internal_test(test_sensors_sliding)
target_compile_definitions(test_sensors_sliding PRIVATE
    ADJUSTOMETER_SLIDING_WINDOW=1 ADJUSTOMETER_PWM_FILTER_US=0)
add_adj_internal_test(test_sensors_pwm_filter)
target_compile_definitions(test_sensors_pwm_filter PRIVATE
    ADJUSTOMETER_SLIDING_WINDOW=1 ADJUSTOMETER_PWM_FILTER_US=7692)

add_adj_internal_test(test_sensors_pwm_filter_200hz test_sensors_pwm_filter.cpp)
target_compile_definitions(test_sensors_pwm_filter_200hz PRIVATE
    ADJUSTOMETER_SLIDING_WINDOW=1 ADJUSTOMETER_PWM_FILTER_US=5000)
