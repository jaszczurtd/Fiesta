#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for the ECU project.
 *
 * This file is automatically picked up by hal_config.h via __has_include.
 * Define HAL_DISABLE_* flags here to exclude unused HAL modules from the
 * build.  Dependency propagation (e.g. EEPROM → KV) is handled by
 * hal_config.h — you only need to disable the base module.
 */

/* ── Modules not used by ECU ──────────────────────────────────────── */

#define HAL_DISABLE_WIFI            /* WiFi — not a Pico W                */
#define HAL_DISABLE_THERMOCOUPLE    /* MCP9600 / MAX6675                  */
#define HAL_DISABLE_UART            /* Hardware UART (SerialUART)         */
#define HAL_DISABLE_DISPLAY         /* ILI9341 TFT driver                  */
#define HAL_DISABLE_RGB_LED         /* RGB LED driver              */
#define HAL_DISABLE_UNITY           /* Unity test framework                 */
