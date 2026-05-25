#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for Fiesta_clock (RP2040 Zero).
 *
 * Opt-in model (HAL >= 1.6.0): only HAL_ENABLE_* modules listed here are
 * compiled in. Core APIs (GPIO, ADC, timer, watchdog) are always
 * available.
 *
 * Note: graphics stay on the legacy lcd.c path for now, so no HAL display
 * backend is enabled.
 */

/* ── Modules used by Fiesta_clock ────────────────────────────────────── */

#define HAL_ENABLE_PCF8563          /* PCF8563 RTC -> RTC, I2C            */
#define HAL_ENABLE_DS18B20          /* DS18B20 1-Wire -> ONEWIRE          */
#define HAL_ENABLE_CAN              /* MCP2515 CAN bus                    */
#define HAL_ENABLE_CRYPTO           /* hal_crypto + hal_sc_auth (SC link) */
