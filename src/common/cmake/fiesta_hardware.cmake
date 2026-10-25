include_guard(GLOBAL)

# fiesta_hardware_header(<out-var> <module dir> <JaszczurHAL root>)
#
# Resolves the assembly of the module's device_tree.json into jh_hardware.h
# and stores the header's path in <out-var>. Host tests and static analysis
# load that header, so they see the JH_HW_* values the firmware is built
# with. Only the early header is written: the mock build keeps its own board
# configuration. A change of the device tree, a binding or the HAL registries
# it read reconfigures the build.
function(fiesta_hardware_header OUT_VAR MODULE_DIR HAL_ROOT)
    include("${HAL_ROOT}/cmake/jh_python.cmake")
    jh_find_python(_fiesta_hw_python)
    set(_fiesta_hw_root "${CMAKE_BINARY_DIR}/.build")
    set(_fiesta_hw_out "${_fiesta_hw_root}/hardware")
    # A module with several assemblies resolves the one its manifest selects,
    # as the firmware does by default; a local developer selection does not
    # apply here.
    set(_fiesta_hw_manifest "${MODULE_DIR}/.vscode/jaszczurhal.project.json")
    set(_fiesta_hw_assembly_args)
    if(EXISTS "${_fiesta_hw_manifest}")
        file(READ "${_fiesta_hw_manifest}" _fiesta_hw_manifest_text)
        string(JSON _fiesta_hw_assembly ERROR_VARIABLE _fiesta_hw_no_assembly
            GET "${_fiesta_hw_manifest_text}" assembly)
        if(NOT _fiesta_hw_no_assembly)
            set(_fiesta_hw_assembly_args --assembly "${_fiesta_hw_assembly}")
        endif()
    endif()
    execute_process(
        COMMAND "${_fiesta_hw_python}" "${HAL_ROOT}/scripts/generate_hardware_config.py"
            resolve --config-dir "${MODULE_DIR}" ${_fiesta_hw_assembly_args}
            --output-root "${_fiesta_hw_root}" --output-dir "${_fiesta_hw_out}"
        RESULT_VARIABLE _fiesta_hw_result
        ERROR_VARIABLE _fiesta_hw_error)
    if(NOT _fiesta_hw_result EQUAL 0)
        string(STRIP "${_fiesta_hw_error}" _fiesta_hw_error)
        message(FATAL_ERROR "Hardware configuration of ${MODULE_DIR} failed: ${_fiesta_hw_error}")
    endif()

    # The model lists every file it read, relative to the module or, with
    # the hal: prefix, to the HAL root; the generator scripts count too.
    file(READ "${_fiesta_hw_out}/jh_hardware_resolved.json" _fiesta_hw_model)
    string(JSON _fiesta_hw_count LENGTH "${_fiesta_hw_model}" dependencies)
    math(EXPR _fiesta_hw_last "${_fiesta_hw_count} - 1")
    file(GLOB _fiesta_hw_inputs "${HAL_ROOT}/scripts/generate_hardware_config.py"
        "${HAL_ROOT}/scripts/hardware_*.py")
    foreach(_fiesta_hw_index RANGE ${_fiesta_hw_last})
        string(JSON _fiesta_hw_path GET "${_fiesta_hw_model}"
            dependencies ${_fiesta_hw_index} path)
        if(_fiesta_hw_path MATCHES "^hal:(.*)$")
            list(APPEND _fiesta_hw_inputs "${HAL_ROOT}/${CMAKE_MATCH_1}")
        else()
            list(APPEND _fiesta_hw_inputs "${MODULE_DIR}/${_fiesta_hw_path}")
        endif()
    endforeach()
    if(EXISTS "${_fiesta_hw_manifest}")
        list(APPEND _fiesta_hw_inputs "${_fiesta_hw_manifest}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_fiesta_hw_inputs})
    set(${OUT_VAR} "${_fiesta_hw_out}/jh_hardware.h" PARENT_SCOPE)
endfunction()

# fiesta_include_hardware_header(<header> <target>...)
#
# Every unit of the targets and of their consumers loads <header> first, as
# the firmware build loads it before the project configuration.
function(fiesta_include_hardware_header HEADER)
    foreach(_fiesta_hw_target IN LISTS ARGN)
        target_compile_options(${_fiesta_hw_target} PUBLIC -include "${HEADER}")
    endforeach()
endfunction()
