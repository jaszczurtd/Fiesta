include_guard(GLOBAL)

function(fiesta_hal_helper_sources OUT_VAR HAL_SRC)
    set(${OUT_VAR}
        ${HAL_SRC}/hal/analog/hal_adc_utils.cpp
        ${HAL_SRC}/hal/core/hal_math.cpp
        ${HAL_SRC}/hal/core/hal_text.cpp
        ${HAL_SRC}/hal/core/jh_endian.cpp
        ${HAL_SRC}/hal/system/hal_periodic_random.cpp
        ${HAL_SRC}/hal/temperature/hal_ntc.cpp
        PARENT_SCOPE
    )
endfunction()
