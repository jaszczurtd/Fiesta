#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for Fiesta_clock (RP2040 Zero).
 *
 * Keep only modules used by Fiesta_clock runtime (GPIO, ADC, I2C, RTC/PCF8563,
 * DS18B20, watchdog) and disable unused features.
 *
 * Note: graphics stay on legacy lcd.c path for now, so HAL display backends are
 * intentionally disabled.
 */

#define HAL_DISABLE_WIFI
#define HAL_DISABLE_EEPROM
#define HAL_DISABLE_GPS
#define HAL_DISABLE_UART
#define HAL_DISABLE_SWSERIAL
#define HAL_DISABLE_CAN
#define HAL_DISABLE_EXTERNAL_ADC
#define HAL_DISABLE_I2C_SLAVE
#define HAL_DISABLE_RGB_LED
#define HAL_DISABLE_THERMOCOUPLE
#define HAL_DISABLE_MCP9600
#define HAL_DISABLE_MAX6675
#define HAL_DISABLE_DISPLAY
#define HAL_DISABLE_TFT
#define HAL_DISABLE_SSD1306
#define HAL_DISABLE_UNITY
