cmake_minimum_required(VERSION 3.16)

project(SerialConfigurator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Werror)
add_compile_definitions(_POSIX_C_SOURCE=200809L)

option(
    SC_USE_JASZCZURHAL_CRYPTO
    "Use JaszczurHAL hal_crypto backend for shared crypto helpers."
    ON
)
set(
    SC_JASZCZURHAL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../libraries/JaszczurHAL"
    CACHE PATH
    "Path to JaszczurHAL repository root."
)

set(SC_JASZCZURHAL_CRYPTO_HEADER "${SC_JASZCZURHAL_DIR}/src/hal/hal_crypto.h")
set(SC_JASZCZURHAL_CRYPTO_SOURCE "${SC_JASZCZURHAL_DIR}/src/hal/hal_crypto.cpp")

set(SC_CRYPTO_SOURCES src/core/sc_crypto_none.c)
set(SC_CRYPTO_INCLUDE_DIRS "")
set(SC_CRYPTO_BACKEND_NAME "none")

set(SC_CRYPTO_DEFINITIONS "")

if(SC_USE_JASZCZURHAL_CRYPTO
   AND EXISTS "${SC_JASZCZURHAL_CRYPTO_HEADER}"
   AND EXISTS "${SC_JASZCZURHAL_CRYPTO_SOURCE}")
    set(
        SC_CRYPTO_SOURCES
        src/core/sc_crypto_jaszczurhal.cpp
        "${SC_JASZCZURHAL_CRYPTO_SOURCE}"
    )
    set(SC_CRYPTO_INCLUDE_DIRS "${SC_JASZCZURHAL_DIR}/src")
    set(SC_CRYPTO_BACKEND_NAME "jaszczurhal")
    # JaszczurHAL hal_crypto is opt-in via HAL_ENABLE_CRYPTO; the SC backend
    # binding pulls the implementation TU directly so we must define the
    # flag here as well, otherwise the header expands to nothing.
    list(APPEND SC_CRYPTO_DEFINITIONS HAL_ENABLE_CRYPTO)
elseif(SC_USE_JASZCZURHAL_CRYPTO)
    message(WARNING
        "SC_USE_JASZCZURHAL_CRYPTO=ON, but hal_crypto was not found under "
        "'${SC_JASZCZURHAL_DIR}'. Falling back to no-op crypto backend."
    )
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 IMPORTED_TARGET gtk4)
if(NOT GTK4_FOUND)
    message(STATUS
        "gtk4 not found via pkg-config; skipping the GUI executable. "
        "Core library, CLI, and CTest targets still build. Install "
        "libgtk-4-dev (or your distro equivalent) to build the GUI shell."
    )
endif()

add_library(serial_configurator_core STATIC
    src/core/sc_auth.c
    src/core/sc_core.c
    src/core/sc_frame.c
    src/core/sc_manifest.c
    src/core/sc_sha256.c
    src/core/sc_transport.c
    ${SC_CRYPTO_SOURCES}
)

target_include_directories(serial_configurator_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    PRIVATE
        ${SC_CRYPTO_INCLUDE_DIRS}
)

target_compile_definitions(serial_configurator_core
    PUBLIC
        SC_CRYPTO_BACKEND_NAME=\"${SC_CRYPTO_BACKEND_NAME}\"
        ${SC_CRYPTO_DEFINITIONS}
)

if(GTK4_FOUND)
    add_executable(serial-configurator
        src/ui/sc_app.c
        src/ui/sc_module_details.c
        src/ui/main.c
    )

    target_link_libraries(serial-configurator
        PRIVATE
            serial_configurator_core
            PkgConfig::GTK4
    )
endif()

add_executable(serial-configurator-cli
    src/cli/sc_cli_main.c
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

    add_executable(serial-configurator-crypto-tests
        tests/test_sc_crypto_bridge.c
    )

    add_executable(serial-configurator-frame-tests
        tests/test_sc_frame.c
    )

    add_executable(serial-configurator-auth-tests
        tests/test_sc_auth.c
    )

    add_executable(serial-configurator-manifest-tests
        tests/test_sc_manifest.c
    )

    add_executable(serial-configurator-phase5-tests
        tests/test_sc_phase5.c
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

    target_link_libraries(serial-configurator-crypto-tests
        PRIVATE
            serial_configurator_core
    )

    target_link_libraries(serial-configurator-frame-tests
        PRIVATE
            serial_configurator_core
    )

    target_link_libraries(serial-configurator-auth-tests
        PRIVATE
            serial_configurator_core
    )

    target_link_libraries(serial-configurator-manifest-tests
        PRIVATE
            serial_configurator_core
    )

    target_link_libraries(serial-configurator-phase5-tests
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

    add_test(
        NAME serial-configurator-crypto-tests
        COMMAND serial-configurator-crypto-tests
    )

    add_test(
        NAME serial-configurator-frame-tests
        COMMAND serial-configurator-frame-tests
    )

    add_test(
        NAME serial-configurator-auth-tests
        COMMAND serial-configurator-auth-tests
    )

    add_test(
        NAME serial-configurator-manifest-tests
        COMMAND serial-configurator-manifest-tests
    )

    add_test(
        NAME serial-configurator-phase5-tests
        COMMAND serial-configurator-phase5-tests
    )
endif()
