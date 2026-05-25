#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for the OilAndSpeed project.
 *
 * Opt-in model (HAL >= 1.6.0): only HAL_ENABLE_* modules listed here are
 * compiled in. Core APIs (GPIO, ADC, PWM, SPI, timer, soft_timer,
 * watchdog, debug, critical_section, hal_serial_session) are always
 * available.
 */

/* ── Modules used by OilAndSpeed ─────────────────────────────────────── */

#define HAL_ENABLE_CAN              /* MCP2515 CAN bus                    */
#define HAL_ENABLE_MCP9600          /* MCP9600 -> THERMOCOUPLE, I2C       */
#define HAL_ENABLE_RGB_LED          /* NeoPixel status LED                */
#define HAL_ENABLE_CRYPTO           /* hal_crypto + hal_sc_auth (SC link) */
