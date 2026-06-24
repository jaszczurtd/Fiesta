cmake_minimum_required(VERSION 3.16)
project(Clocks_STM32G474 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CLOCKS_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
get_filename_component(CLOCKS_DIR "${CLOCKS_DIR}" ABSOLUTE)
set(FIESTA_SRC_DIR "${CLOCKS_DIR}/..")
get_filename_component(FIESTA_SRC_DIR "${FIESTA_SRC_DIR}" ABSOLUTE)

set(JASZCZURHAL_ROOT "${CLOCKS_DIR}/../../../libraries/JaszczurHAL"
    CACHE PATH "Path to the JaszczurHAL repository")
get_filename_component(JASZCZURHAL_ROOT "${JASZCZURHAL_ROOT}" ABSOLUTE)

if(NOT EXISTS "${JASZCZURHAL_ROOT}/src/hal/hal.h")
    message(FATAL_ERROR "JaszczurHAL not found: ${JASZCZURHAL_ROOT}")
endif()


set(JH_SRC "${JASZCZURHAL_ROOT}/src")
set(G474 "${JH_SRC}/hal/impl/stm32g474")
set(LDSCRIPT "${JASZCZURHAL_ROOT}/stm32_lib/STM32G474RETx_FLASH.ld")

file(GLOB STM32_IMPL CONFIGURE_DEPENDS "${G474}/*.cpp")
file(GLOB STM32_DRIVERS CONFIGURE_DEPENDS "${G474}/drivers/*/*.cpp")
file(GLOB_RECURSE STM32_SHARED CONFIGURE_DEPENDS
    "${JH_SRC}/hal/impl/shared/*.cpp"
    "${JH_SRC}/hal/impl/shared/*.c"
)

set(HAL_COMMON
    "${JH_SRC}/hal/hal_config.cpp"
    "${JH_SRC}/hal/hal_can_util.cpp"
    "${JH_SRC}/hal/hal_crypto.cpp"
    "${JH_SRC}/hal/hal_pga2311.cpp"
    "${JH_SRC}/hal/hal_digipot.cpp"
    "${JH_SRC}/hal/hal_kv.cpp"
    "${JH_SRC}/hal/hal_timer_ext.cpp"
    "${JH_SRC}/hal/hal_soft_timer.cpp"
    "${JH_SRC}/hal/hal_pid_controller.cpp"
    "${JH_SRC}/hal_app_entry.cpp"
)

set(UTILS
    "${JH_SRC}/utils/tools.cpp"
    "${JH_SRC}/hal/impl/shared/frameworks/smart_timers/SmartTimers.cpp"
    "${JH_SRC}/utils/multicoreWatchdog.cpp"
    "${JH_SRC}/utils/pidController.cpp"
    "${JH_SRC}/utils/draw7Segment.cpp"
    "${JH_SRC}/hal/impl/shared/frameworks/cjson/cJSON.c"
    "${JH_SRC}/hal/impl/shared/frameworks/cjson/cJSON_Utils.c"
)

set(CLOCKS_SOURCES
    "${CLOCKS_DIR}/Cluster.cpp"
    "${CLOCKS_DIR}/TFTExtension.cpp"
    "${CLOCKS_DIR}/buzzer.cpp"
    "${CLOCKS_DIR}/buzzerStrategy.cpp"
    "${CLOCKS_DIR}/can.cpp"
    "${CLOCKS_DIR}/config.cpp"
    "${CLOCKS_DIR}/engineFuel.cpp"
    "${CLOCKS_DIR}/logic.cpp"
    "${CLOCKS_DIR}/peripherials.cpp"
    "${CLOCKS_DIR}/pressureGauge.cpp"
    "${CLOCKS_DIR}/simpleGauge.cpp"
    "${CLOCKS_DIR}/tempGauge.cpp"
    "${CLOCKS_DIR}/sc_param_handlers_glue.c"
    "${CMAKE_CURRENT_LIST_DIR}/clocks_hal_app_adapter.cpp"
)

add_executable(Clocks_STM32G474
    ${CLOCKS_SOURCES}
    "${G474}/port/startup_stm32g474.c"
    "${G474}/port/system_stm32g474.c"
    "${G474}/port/g474_debug_uart.c"
    "${G474}/port/exception_info.c"
    "${G474}/port/atomic_stubs_cm4.c"
    "${G474}/port/runtime/stm32g474_syscalls.c"
    ${STM32_IMPL}
    ${STM32_DRIVERS}
    ${STM32_SHARED}
    ${HAL_COMMON}
    ${UTILS}
)

set_target_properties(Clocks_STM32G474 PROPERTIES
    OUTPUT_NAME "Clocks_STM32G474"
    SUFFIX ".elf"
    LINKER_LANGUAGE CXX
)

target_include_directories(Clocks_STM32G474 PRIVATE
    "${CLOCKS_DIR}"
    "${FIESTA_SRC_DIR}"
    "${FIESTA_SRC_DIR}/common/canDefinitions"
    "${FIESTA_SRC_DIR}/common/scDefinitions"
    "${JH_SRC}"
    "${JH_SRC}/hal"
    "${JH_SRC}/arduino_host_stubs"
    "${G474}"
)

target_compile_definitions(Clocks_STM32G474 PRIVATE
    HAL_TARGET_STM32G474=1
    HAL_PROVIDE_APP_ENTRY=1
    HAL_ENABLE_APP_TASK1=1
)

set(ARCH_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
target_compile_options(Clocks_STM32G474 PRIVATE
    ${ARCH_FLAGS}
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -include "${JH_SRC}/hal/hal_target.h"
    $<$<COMPILE_LANGUAGE:C>:-ffreestanding>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
)

target_link_options(Clocks_STM32G474 PRIVATE
    ${ARCH_FLAGS}
    "-T${LDSCRIPT}"
    -nostartfiles
    -Wl,--gc-sections
    --specs=nano.specs
    --specs=nosys.specs
)

if(CMAKE_OBJCOPY)
    add_custom_command(TARGET Clocks_STM32G474 POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:Clocks_STM32G474>"
            "${CMAKE_CURRENT_BINARY_DIR}/Clocks_STM32G474.bin"
        COMMAND "${CMAKE_OBJCOPY}" -O ihex
            "$<TARGET_FILE:Clocks_STM32G474>"
            "${CMAKE_CURRENT_BINARY_DIR}/Clocks_STM32G474.hex"
        VERBATIM
    )
endif()
