cmake_minimum_required(VERSION 3.16)

project(SerialConfigurator VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Werror)
add_compile_definitions(_POSIX_C_SOURCE=200809L)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

add_library(serial_configurator_core STATIC
    src/core/sc_core.c
    src/core/sc_transport.c
)

target_include_directories(serial_configurator_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

add_executable(serial-configurator
    src/ui/sc_app.c
    src/ui/sc_module_details.c
    src/ui/main.c
)

add_executable(serial-configurator-cli
    src/cli/sc_cli_main.c
)

target_link_libraries(serial-configurator
    PRIVATE
        serial_configurator_core
        PkgConfig::GTK4
)

target_link_libraries(serial-configurator-cli
    PRIVATE
        serial_configurator_core
)

include(CTest)

if(BUILD_TESTING)
    add_executable(serial-configurator-core-tests
        tests/test_sc_core_smoke.c
    )

    add_executable(serial-configurator-core-api-tests
        tests/test_sc_core_api_contract.c
    )

    add_executable(serial-configurator-core-protocol-tests
        tests/test_sc_core_protocol_readonly.c
    )

    target_link_libraries(serial-configurator-core-tests
        PRIVATE
            serial_configurator_core
    )

    target_link_libraries(serial-configurator-core-api-tests
        PRIVATE
            serial_configurator_core
    )

    target_link_libraries(serial-configurator-core-protocol-tests
        PRIVATE
            serial_configurator_core
    )

    add_test(
        NAME serial-configurator-core-tests
        COMMAND serial-configurator-core-tests
    )

    add_test(
        NAME serial-configurator-core-api-tests
        COMMAND serial-configurator-core-api-tests
    )

    add_test(
        NAME serial-configurator-core-protocol-tests
        COMMAND serial-configurator-core-protocol-tests
    )
endif()
