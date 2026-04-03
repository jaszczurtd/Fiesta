# Helper: create a test executable linked against ecu_mock
function(add_ecu_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE ecu_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_ecu_test(test_glowPlugs)
add_ecu_test(test_engineFan)
add_ecu_test(test_engineHeater)
add_ecu_test(test_sensors_calc)
add_ecu_test(test_obd2)
add_ecu_test(test_can)
