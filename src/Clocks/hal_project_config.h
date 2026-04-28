#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for the Clocks project.
 *
 * This file is automatically picked up by hal_config.h via __has_include.
 * Define HAL_DISABLE_* flags here to exclude unused HAL modules from the
 * build.  Dependency propagation (e.g. EEPROM -> KV) is handled by
 * hal_config.h - you only need to disable the base module.
 */

/* ── Opt-in modules ───────────────────────────────────────────────── */

#define HAL_ENABLE_CRYPTO           /* hal_crypto + hal_sc_auth - needed
                                       for SC_GET_META base64 reply and
                                       the SC_AUTH challenge/response   */

/* ── Modules not used by Clocks ──────────────────────────────────────── */

#define HAL_DISABLE_WIFI            /* WiFi - not a Pico W                */
#define HAL_DISABLE_EEPROM          /* EEPROM / AT24C256 - propagates KV  */
#define HAL_DISABLE_GPS             /* GPS / NMEA receiver                */
#define HAL_DISABLE_THERMOCOUPLE    /* MCP9600 / MAX6675                  */
#define HAL_DISABLE_UART            /* Hardware UART (SerialUART)         */
#define HAL_DISABLE_SWSERIAL        /* SoftwareSerial                     */
#define HAL_DISABLE_I2C             /* I2C bus - propagates EXTERNAL_ADC  */
#define HAL_DISPLAY_ILI9341         /* ILI9341 TFT driver                  */
#define HAL_DISABLE_SSD1306         /* SSD1306 OLED driver                 */
