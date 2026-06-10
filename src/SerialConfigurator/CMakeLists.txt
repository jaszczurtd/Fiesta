cmake_minimum_required(VERSION 3.16)

project(SerialConfigurator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

add_compile_options(-Wall -Wextra -Werror)
add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Valgrind / CTest memcheck configuration (copied from JaszczurHAL pattern).
# Host-only dynamic analysis for the unit-test suite:
#   ctest --test-dir build -T memcheck --output-on-failure
find_program(MEMORYCHECK_COMMAND NAMES valgrind)
set(MEMORYCHECK_COMMAND_OPTIONS
    "--leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --track-origins=yes --error-exitcode=1"
    CACHE STRING "Flags passed to Valgrind by 'ctest -T memcheck'")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind.supp")
    set(_SC_VALGRIND_SUPP_FULL
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind.supp")
    set(_SC_VALGRIND_SUPP_LEGACY
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind-legacy.supp")

    set(_SC_VALGRIND_SUPP_SELECTED "${_SC_VALGRIND_SUPP_FULL}")

    # Some older Valgrind builds reject newer suppression classes used by
    # NVIDIA-specific noise filters (e.g. Memcheck:ReallocZero). Probe the
    # suppression parser at configure time and transparently fall back to a
    # legacy-compatible suppression file when needed.
    if(MEMORYCHECK_COMMAND AND EXISTS "${_SC_VALGRIND_SUPP_LEGACY}")
        execute_process(
            COMMAND "${MEMORYCHECK_COMMAND}"
                    "--tool=memcheck"
                    "--leak-check=no"
                    "--quiet"
                    "--suppressions=${_SC_VALGRIND_SUPP_FULL}"
                    "${CMAKE_COMMAND}" -E true
            RESULT_VARIABLE _SC_VALGRIND_SUPP_PROBE_RC
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(NOT _SC_VALGRIND_SUPP_PROBE_RC EQUAL 0)
            message(STATUS
                "Valgrind rejected tests/valgrind.supp; using legacy-compatible suppressions")
            set(_SC_VALGRIND_SUPP_SELECTED "${_SC_VALGRIND_SUPP_LEGACY}")
        endif()
    endif()

    set(MEMORYCHECK_SUPPRESSIONS_FILE
        "${_SC_VALGRIND_SUPP_SELECTED}"
    CACHE FILEPATH "Valgrind suppressions for known third-party/system noise" FORCE)
endif()

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
set(SC_JASZCZURHAL_WIREGUARD_CRYPTO_SOURCES
    "${SC_JASZCZURHAL_DIR}/src/hal/impl/shared/wireguard/crypto/chacha20.c"
    "${SC_JASZCZURHAL_DIR}/src/hal/impl/shared/wireguard/crypto/chacha20poly1305.c"
    "${SC_JASZCZURHAL_DIR}/src/hal/impl/shared/wireguard/crypto/crypto.c"
    "${SC_JASZCZURHAL_DIR}/src/hal/impl/shared/wireguard/crypto/poly1305-donna.c"
)

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
        ${SC_JASZCZURHAL_WIREGUARD_CRYPTO_SOURCES}
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

# Phase 8.7 - libshumate map widget used by the optional Map tab. The
# Map tab degrades to a "feature unavailable" placeholder when shumate
# is missing so the rest of the GUI still builds on minimal hosts.
pkg_check_modules(SHUMATE IMPORTED_TARGET shumate-1.0)
if(NOT SHUMATE_FOUND)
    message(STATUS
        "shumate-1.0 not found via pkg-config; the Map tab will show a "
        "placeholder instead of a live map. Install libshumate-dev "
        "(or your distro equivalent) to enable the GPS map view."
    )
endif()

set(FIESTA_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common")
set(FIESTA_SC_DEFINITIONS_DIR "${FIESTA_COMMON_DIR}/scDefinitions")

# R1.1: Single-source-of-truth library for the SC wire vocabulary plus the
# generic descriptor-driven SC reply machinery. Shared by host (SC core,
# CLI, host tests starting in R1.3) and firmware modules (ECU/Clocks/
# OilAndSpeed migrating onto it from R1.2 onward via arduino-build.sh).
#
# Intentionally JaszczurHAL-free: sc_param_handlers.c includes only the
# HAL-free sc_protocol.h. The HAL-bound fiesta_default_vocabulary lives in
# sc_session_vocabulary.h, which firmware modules include directly via
# arduino-build.sh - host CI runs that fall back to sc_crypto_none don't
# need it and must not require JaszczurHAL on the include path.
add_library(fiesta_sc_definitions STATIC
    "${FIESTA_SC_DEFINITIONS_DIR}/sc_param_handlers.c"
)
target_include_directories(fiesta_sc_definitions
    PUBLIC
        "${FIESTA_SC_DEFINITIONS_DIR}"
)

add_library(serial_configurator_core STATIC
    src/core/sc_auth.c
    src/core/sc_core.c
    src/core/sc_flash.c
    src/core/sc_frame.c
    src/core/sc_gps.c
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

# R1.3: SC core / CLI / transport adopt SSOT defines from sc_protocol.h
# (no more inline "SC_GET_META" / "SC_OK" string literals). The link
# also exposes sc_protocol.h on the include path of every target that
# links serial_configurator_core, since fiesta_sc_definitions PUBLIC
# includes its header dir.
target_link_libraries(serial_configurator_core
    PUBLIC
        fiesta_sc_definitions
)

target_compile_definitions(serial_configurator_core
    PUBLIC
        SC_CRYPTO_BACKEND_NAME=\"${SC_CRYPTO_BACKEND_NAME}\"
        ${SC_CRYPTO_DEFINITIONS}
)

if(GTK4_FOUND)
    add_executable(serial-configurator
        src/ui/sc_app.c
        src/ui/sc_detection.c
        src/ui/sc_flash_paths.c
        src/ui/sc_flash_tab.c
        src/ui/sc_generic_gfx_helper.c
        src/ui/sc_i18n.c
        src/ui/sc_map_tab.c
        src/ui/sc_module_details.c
        src/ui/sc_modules_view.c
        src/ui/sc_progressbar.c
        src/ui/sc_values_tab.c
        src/ui/main.c
    )

    target_link_libraries(serial-configurator
        PRIVATE
            serial_configurator_core
            PkgConfig::GTK4
    )

    if(SHUMATE_FOUND)
        target_compile_definitions(serial-configurator
            PRIVATE
                SC_HAVE_SHUMATE=1
        )
        target_link_libraries(serial-configurator
            PRIVATE
                PkgConfig::SHUMATE
        )
    endif()

    install(TARGETS serial-configurator
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(FILES packaging/serial-configurator.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    )

    install(FILES scripts/icon.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pixmaps
        RENAME serial-configurator.png
    )
endif()

add_executable(serial-configurator-cli
    src/cli/sc_cli_commands.c
    src/cli/sc_cli_main.c
    src/cli/sc_cli_output.c
    src/cli/sc_cli_selectors.c
)

target_link_libraries(serial-configurator-cli
    PRIVATE
        serial_configurator_core
)

install(TARGETS serial-configurator-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "serial-configurator")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Fiesta Serial Configurator desktop application and CLI"
)
set(CPACK_PACKAGE_VENDOR "Fiesta")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Fiesta maintainers")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Fiesta maintainers")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

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

    add_executable(serial-configurator-phase8-host-tests
        tests/test_sc_phase8_host.c
    )

    add_executable(serial-configurator-i18n-tests
        tests/test_sc_i18n.c
    )

    add_executable(serial-configurator-flash-tests
        tests/test_sc_flash.c
    )

    add_executable(serial-configurator-flash-paths-tests
        tests/test_sc_flash_paths.c
    )

    add_executable(serial-configurator-sc-param-tests
        tests/test_sc_param.c
    )
    target_link_libraries(serial-configurator-sc-param-tests
        PRIVATE
            fiesta_sc_definitions
    )

    add_executable(serial-configurator-sc-gps-tests
        tests/test_sc_gps.c
    )
    target_link_libraries(serial-configurator-sc-gps-tests
        PRIVATE
            serial_configurator_core
    )

    # Phase 6.3 - BOOTSEL drive watcher. Uses pthread for the
    # mid-test mkdir worker and the public sc_flash API from
    # serial_configurator_core.
    find_package(Threads REQUIRED)
    add_executable(serial-configurator-flash-bootsel-tests
        tests/test_sc_flash_bootsel.c
    )
    target_link_libraries(serial-configurator-flash-bootsel-tests
        PRIVATE
            serial_configurator_core
            Threads::Threads
    )

    # Phase 6.4 - UF2 copy with progress + re-enumeration waiter.
    # Same fixture pattern as bootsel: mkdtemp parent + pthread
    # worker that drops the expected entry mid-test.
    add_executable(serial-configurator-flash-copy-reenum-tests
        tests/test_sc_flash_copy_reenum.c
    )
    target_link_libraries(serial-configurator-flash-copy-reenum-tests
        PRIVATE
            serial_configurator_core
            Threads::Threads
    )

    # Phase 6.5 - end-to-end orchestrator (sc_core_flash). Drives
    # the full pipeline through a mock transport + mkdtemp BOOTSEL /
    # by-id fixtures so every branch is hermetic.
    add_executable(serial-configurator-flash-orchestrator-tests
        tests/test_sc_flash_orchestrator.c
    )
    target_link_libraries(serial-configurator-flash-orchestrator-tests
        PRIVATE
            serial_configurator_core
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

    target_link_libraries(serial-configurator-phase8-host-tests
        PRIVATE
            serial_configurator_core
    )

    # i18n test embeds src/ui/sc_i18n.c by #include - it is pure C with no
    # GTK or core deps, so the test target stands alone (linkable on
    # hosts that lack libgtk-4-dev).
    target_include_directories(serial-configurator-i18n-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(serial-configurator-flash-tests
        PRIVATE
            serial_configurator_core
    )

    # flash-paths test embeds src/ui/sc_flash_paths.c by #include - it
    # is pure C with no GTK dependency, so the test target stands alone
    # exactly like the i18n tests do.
    target_link_libraries(serial-configurator-flash-paths-tests
        PRIVATE
            serial_configurator_core
    )
    target_include_directories(serial-configurator-flash-paths-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Custom progress-bar widget test - embeds src/ui/sc_progressbar.c
    # by #include. Unlike the i18n test, the widget is GTK-backed
    # (GtkDrawingArea + cairo), so the target only registers when GTK4
    # is available on the host. The test itself skips at runtime when
    # no display is reachable, so headless CI runs do not fail.
    if(GTK4_FOUND)
        add_executable(serial-configurator-progressbar-tests
            tests/test_sc_progressbar.c
        )
        target_link_libraries(serial-configurator-progressbar-tests
            PRIVATE
                serial_configurator_core
                PkgConfig::GTK4
        )
        target_include_directories(serial-configurator-progressbar-tests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
        )

        add_test(
            NAME serial-configurator-progressbar-tests
            COMMAND serial-configurator-progressbar-tests
        )
    endif()

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

    add_test(
        NAME serial-configurator-phase8-host-tests
        COMMAND serial-configurator-phase8-host-tests
    )

    add_test(
        NAME serial-configurator-i18n-tests
        COMMAND serial-configurator-i18n-tests
    )

    add_test(
        NAME serial-configurator-flash-tests
        COMMAND serial-configurator-flash-tests
    )

    add_test(
        NAME serial-configurator-flash-paths-tests
        COMMAND serial-configurator-flash-paths-tests
    )

    add_test(
        NAME serial-configurator-sc-param-tests
        COMMAND serial-configurator-sc-param-tests
    )

    add_test(
        NAME serial-configurator-sc-gps-tests
        COMMAND serial-configurator-sc-gps-tests
    )

    add_test(
        NAME serial-configurator-flash-bootsel-tests
        COMMAND serial-configurator-flash-bootsel-tests
    )

    add_test(
        NAME serial-configurator-flash-copy-reenum-tests
        COMMAND serial-configurator-flash-copy-reenum-tests
    )

    add_test(
        NAME serial-configurator-flash-orchestrator-tests
        COMMAND serial-configurator-flash-orchestrator-tests
    )
endif()

# Optional analysis helpers kept outside the default build.
option(SERIALCONFIGURATOR_ENABLE_VALGRIND_TARGET "Enable check-valgrind custom target" ON)
if(SERIALCONFIGURATOR_ENABLE_VALGRIND_TARGET AND MEMORYCHECK_COMMAND)
    add_custom_target(check-valgrind
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} -T memcheck --output-on-failure
        COMMENT "Running CTest memcheck (Valgrind) for SerialConfigurator"
        VERBATIM
    )
endif()

find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy)
option(SERIALCONFIGURATOR_ENABLE_CLANG_TIDY_TARGET "Enable check-clang-tidy custom target" ON)
if(SERIALCONFIGURATOR_ENABLE_CLANG_TIDY_TARGET AND RUN_CLANG_TIDY_EXECUTABLE)
    add_custom_target(check-clang-tidy
        COMMAND ${RUN_CLANG_TIDY_EXECUTABLE}
            -p ${CMAKE_BINARY_DIR}
            -quiet
            -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
            "(^.*/src/SerialConfigurator/(src|tests)/.*\\.(c|cpp)$)|(^.*/src/common/scDefinitions/.*\\.c$)"
        COMMENT "Running clang-tidy for SerialConfigurator host-compilable sources"
        VERBATIM
    )
endif()

include(CPack)
