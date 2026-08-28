#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for the ECU project.
 *
 * Opt-in model (HAL >= 1.6.0): only HAL_ENABLE_* modules listed here are
 * compiled in. Core APIs (GPIO, ADC, PWM, SPI, timer, soft_timer,
 * watchdog, debug, mutex, critical_section, hal_serial, hal_pid_controller,
 * hal_time_from_components) are always available.
 */

/* ── Modules used by ECU ─────────────────────────────────────────────── */

#define HAL_ENABLE_I2C /* I2C master (sensors + AT24C256)    */
#ifndef HAL_ENABLE_KV
#define HAL_ENABLE_KV /* KV store -> EEPROM                 */
#endif
#define HAL_ENABLE_CAN      /* Generic CAN API facade             */
#define HAL_ENABLE_MCP2515  /* MCP2515 CAN backend                */
#define HAL_ENABLE_SWSERIAL /* Software serial (GPS)               */
#define HAL_ENABLE_GPS      /* TinyGPS++ -> SWSERIAL              */
#define HAL_ENABLE_PWM_FREQ /* Frequency-controlled PWM           */
#define HAL_ENABLE_CRYPTO   /* hal_crypto + hal_sc_auth (SC link) */
#define HAL_ENABLE_SERIAL_COMMANDS
#define HAL_COMMAND_ROUTER_MAX_COMMANDS 16u
#define HAL_ENABLE_APP_TASK1

/* ECU persistence uses the full 32 KiB flash-backed EEPROM reservation. */
#ifndef HAL_RP_FLASH_EEPROM_SIZE
#define HAL_RP_FLASH_EEPROM_SIZE 32768
#endif

/* The transaction engine applies this per coordination phase. Three bounded
 * waits plus the measured 32 KiB erase/program stay below the 4 s watchdog. */
#define HAL_RP_FLASH_TRANSACTION_TIMEOUT_MS 750u

/* Native RP system stacks, in bytes. */
#define HAL_RP_CORE0_STACK_SIZE 4096u
#define HAL_RP_CORE1_STACK_SIZE 4096u
