#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for Fiesta_clock (RP2040 Zero).
 *
 * Opt-in model (HAL >= 1.6.0): only HAL_ENABLE_* modules listed here are
 * compiled in. Core APIs (GPIO, ADC, timer, watchdog) are always
 * available.
 *
 */

/* ── Modules used by Fiesta_clock ────────────────────────────────────── */

/* Select one RTC. An external RTC_ds3231 definition overrides the default. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(DS3231, "Fiesta_clock with a DS3231 RTC", RTC_ds3231 = 1)
#if !defined(RTC_pcf8563) && !defined(RTC_ds3231)
#define RTC_pcf8563
#endif
#if defined(RTC_pcf8563) && defined(RTC_ds3231)
#error "Select exactly one Fiesta_clock RTC"
#endif
#if defined(RTC_ds3231)
#define HAL_ENABLE_DS3231
#else
#define HAL_ENABLE_PCF8563
#endif
#define HAL_ENABLE_SSD1306 /* OLED -> DISPLAY, I2C              */
#define HAL_ENABLE_DS18B20 /* DS18B20 1-Wire -> ONEWIRE          */
#define HAL_ENABLE_CAN     /* Generic CAN API facade             */
#define HAL_ENABLE_MCP2515 /* MCP2515 CAN backend                */
#define HAL_ENABLE_CRYPTO  /* hal_crypto + hal_sc_auth (SC link) */
#define HAL_ENABLE_KV      /* UTC marker of the RTC -> flash    */
#define HAL_ENABLE_SERIAL_COMMANDS
#define HAL_COMMAND_ROUTER_MAX_COMMANDS 16u

/* Two 4 KiB KV banks fill the flash reservation, which CMake reads here.
 * The store holds a single key. */
#ifndef HAL_RP_FLASH_EEPROM_SIZE
#define HAL_RP_FLASH_EEPROM_SIZE 8192
#endif
#define HAL_KV_MAX_KEYS 4u
/* The transaction engine applies this per coordination phase; three bounded
 * waits and a sector erase stay below the 2 s watchdog. */
#define HAL_RP_FLASH_TRANSACTION_TIMEOUT_MS 500u
