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

#define HAL_ENABLE_I2C              /* I2C master (sensors + AT24C256)    */
#define HAL_ENABLE_KV               /* KV store -> EEPROM                 */
#define HAL_ENABLE_CAN              /* MCP2515 CAN bus                    */
#define HAL_ENABLE_SWSERIAL         /* Software serial (GPS)               */
#define HAL_ENABLE_GPS              /* TinyGPS++ -> SWSERIAL              */
#define HAL_ENABLE_PWM_FREQ         /* Frequency-controlled PWM           */
#define HAL_ENABLE_CRYPTO           /* hal_crypto + hal_sc_auth (SC link) */
