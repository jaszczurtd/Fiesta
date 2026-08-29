function(fiesta_hal_command_sources OUT_VAR HAL_SOURCE_DIR)
    set(${OUT_VAR}
        "${HAL_SOURCE_DIR}/hal/commands/hal_command_router.cpp"
        "${HAL_SOURCE_DIR}/hal/commands/hal_command_wire.cpp"
        "${HAL_SOURCE_DIR}/hal/commands/jh_command_adapter_internal.cpp"
        PARENT_SCOPE
    )
endfunction()
