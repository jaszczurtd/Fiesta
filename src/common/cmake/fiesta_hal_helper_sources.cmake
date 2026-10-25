include_guard(GLOBAL)

# fiesta_hal_include_dirs(<out> <JaszczurHAL root>): the headers of the
# frameworks JaszczurHAL manages, plus jhbase, whose status codes and COUNTOF
# hal/core/hal_status.h and hal/core/hal_array.h include. A macro, so the HAL
# module it includes keeps its state in the caller's directory scope.
macro(fiesta_hal_include_dirs OUT_VAR HAL_ROOT)
    include("${HAL_ROOT}/cmake/jh_jhbase.cmake")
    jh_managed_framework_include_dirs(_fiesta_managed_dirs)
    jh_jhbase_include_dir(_fiesta_jhbase_dir)
    set(${OUT_VAR} ${_fiesta_managed_dirs} "${_fiesta_jhbase_dir}")
    unset(_fiesta_managed_dirs)
    unset(_fiesta_jhbase_dir)
endmacro()

function(fiesta_hal_helper_sources OUT_VAR HAL_SRC)
    set(${OUT_VAR}
        ${HAL_SRC}/hal/analog/hal_adc_utils.cpp
        ${HAL_SRC}/hal/core/hal_math.cpp
        ${HAL_SRC}/hal/core/hal_text.cpp
        ${HAL_SRC}/hal/core/jh_endian.cpp
        ${HAL_SRC}/hal/system/hal_device_id.cpp
        ${HAL_SRC}/hal/system/hal_periodic_random.cpp
        ${HAL_SRC}/hal/temperature/hal_ntc.cpp
        PARENT_SCOPE
    )
endfunction()
