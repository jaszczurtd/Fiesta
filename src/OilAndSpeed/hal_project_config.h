#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for the OilAndSpeed project.
 *
 * This file is automatically picked up by hal_config.h via __has_include.
 * Define HAL_DISABLE_* flags here to exclude unused HAL modules from the
 * build.  Dependency propagation (e.g. EEPROM → KV) is handled by
 * hal_config.h — you only need to disable the base module.
 */

/* ── Modules not used by OilAndSpeed ──────────────────────────────────────── */

#define HAL_DISABLE_WIFI            /* WiFi — not a Pico W                */
#define HAL_DISABLE_EEPROM          /* EEPROM / AT24C256 — propagates KV  */
#define HAL_DISABLE_GPS             /* GPS / NMEA receiver                */
#define HAL_DISABLE_UART            /* Hardware UART (SerialUART)         */
#define HAL_DISABLE_SWSERIAL        /* SoftwareSerial                     */
#define HAL_DISABLE_DISPLAY         /* GFX graphics library                */
#define HAL_DISABLE_MAX6675         /* MAX6675 thermocouple interface      */
#define HAL_DISABLE_EXTERNAL_ADC    /* External ADC                        */
#define HAL_DISABLE_UNITY          /* Unity unit testing framework         */
