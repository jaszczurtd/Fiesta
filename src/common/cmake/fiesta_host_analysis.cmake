include_guard(GLOBAL)

# Valgrind (CTest memcheck) and clang-tidy for module host tests; never part of
# a firmware build. Include before include(CTest), which reads the memcheck
# variables:
#   ctest --test-dir build_test -T memcheck --output-on-failure
find_program(MEMORYCHECK_COMMAND NAMES valgrind)
set(MEMORYCHECK_COMMAND_OPTIONS
    "--leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --track-origins=yes --error-exitcode=1"
    CACHE STRING "Flags passed to Valgrind by 'ctest -T memcheck'")
# The module's tests/valgrind.supp, or empty so that a removed file leaves no
# stale cache value.
set(_fiesta_valgrind_supp "${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind.supp")
if(NOT EXISTS "${_fiesta_valgrind_supp}")
    set(_fiesta_valgrind_supp "")
endif()
set(MEMORYCHECK_SUPPRESSIONS_FILE "${_fiesta_valgrind_supp}"
    CACHE FILEPATH "Valgrind suppressions for known third-party/system noise" FORCE)
unset(_fiesta_valgrind_supp)

find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy)
set_property(GLOBAL PROPERTY FIESTA_MEMCHECK_RUNNER
    "${CMAKE_CURRENT_LIST_DIR}/fiesta_run_memcheck.cmake")

# Adds check-valgrind, memcheck over the module tests except the labels that
# match MEMCHECK_EXCLUDE_LABELS, and check-clang-tidy over the compiled sources
# that match CLANG_TIDY_FILES. <PREFIX>_ENABLE_VALGRIND_TARGET and
# <PREFIX>_ENABLE_CLANG_TIDY_TARGET turn them off.
function(fiesta_add_host_analysis_targets MODULE PREFIX)
    cmake_parse_arguments(PARSE_ARGV 2 ARG ""
        "CLANG_TIDY_FILES;MEMCHECK_EXCLUDE_LABELS" "")
    if(NOT ARG_CLANG_TIDY_FILES)
        message(FATAL_ERROR "${MODULE}: CLANG_TIDY_FILES is required")
    endif()

    option(${PREFIX}_ENABLE_VALGRIND_TARGET "Enable check-valgrind custom target" ON)
    if(${PREFIX}_ENABLE_VALGRIND_TARGET AND MEMORYCHECK_COMMAND)
        get_property(memcheck_runner GLOBAL PROPERTY FIESTA_MEMCHECK_RUNNER)
        add_custom_target(check-valgrind
            COMMAND ${CMAKE_COMMAND}
                "-DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
                "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DEXCLUDE_LABELS=${ARG_MEMCHECK_EXCLUDE_LABELS}"
                -P "${memcheck_runner}"
            COMMENT "Running CTest memcheck (Valgrind) for ${MODULE}"
            VERBATIM
        )
    endif()

    option(${PREFIX}_ENABLE_CLANG_TIDY_TARGET "Enable check-clang-tidy custom target" ON)
    if(${PREFIX}_ENABLE_CLANG_TIDY_TARGET AND RUN_CLANG_TIDY_EXECUTABLE)
        add_custom_target(check-clang-tidy
            COMMAND ${RUN_CLANG_TIDY_EXECUTABLE}
                -p ${CMAKE_BINARY_DIR}
                -quiet
                -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
                "${ARG_CLANG_TIDY_FILES}"
            COMMENT "Running clang-tidy for ${MODULE} host-compilable sources"
            VERBATIM
        )
    endif()
endfunction()
