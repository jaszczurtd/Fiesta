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
    include("${HAL_ROOT}/cmake/jh_hardware_config.cmake")
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
            set(_fiesta_hw_assembly_args ASSEMBLY "${_fiesta_hw_assembly}")
        endif()
    endif()
    jh_resolve_hardware_header(_fiesta_hw_header ROOT "${HAL_ROOT}"
        CONFIG_DIR "${MODULE_DIR}" ${_fiesta_hw_assembly_args}
        OUTPUT_ROOT "${_fiesta_hw_root}" OUTPUT_DIR "${_fiesta_hw_out}")
    if(EXISTS "${_fiesta_hw_manifest}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_fiesta_hw_manifest}")
    endif()
    set(${OUT_VAR} "${_fiesta_hw_header}" PARENT_SCOPE)
endfunction()

# fiesta_include_hardware_header(<header> <target>...)
#
# Every unit of the targets and of their consumers loads <header> first, as
# the firmware build loads it before the project configuration.
function(fiesta_include_hardware_header HEADER)
    foreach(_fiesta_hw_target IN LISTS ARGN)
        if(MSVC)
            target_compile_options(${_fiesta_hw_target} PUBLIC "/FI${HEADER}")
        else()
            target_compile_options(${_fiesta_hw_target} PUBLIC -include "${HEADER}")
        endif()
    endforeach()
endfunction()
