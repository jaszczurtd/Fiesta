#pragma once

/**
 * @file tests/include/hal_project_config.h
 * @brief Test-only HAL configuration override.
 *
 * The .mock backend in JaszczurHAL references types from every optional
 * HAL module unconditionally (hal_can_t, hal_rgb_led_color_t,
 * hal_pwm_freq_channel_t, ...). To compile the mock library on host we
 * therefore enable the full HAL_ENABLE_* matrix - mirroring the host
 * hal_mock target declared in JaszczurHAL/CMakeLists.txt.
 *
 * Picked up before the real Clocks/hal_project_config.h via
 * target_include_directories(... BEFORE PUBLIC ${SRC}/tests/include) in
 * the project CMakeLists.txt.
 */

#define HAL_ENABLE_WIFI
#define HAL_ENABLE_TIME
#define HAL_ENABLE_MQTT
#define HAL_ENABLE_UDP
#define HAL_ENABLE_OTA
#define HAL_ENABLE_WIREGUARD
#define HAL_ENABLE_EEPROM
#define HAL_ENABLE_KV
#define HAL_ENABLE_LITTLEFS
#define HAL_ENABLE_UART
#define HAL_ENABLE_SWSERIAL
#define HAL_ENABLE_I2C
#define HAL_ENABLE_I2C_SLAVE
#define HAL_ENABLE_CAN
#define HAL_ENABLE_MCP2515
#define HAL_ENABLE_PCF8563 /* -> RTC + I2C */
#define HAL_ENABLE_DS3231
#define HAL_ENABLE_MCP9600 /* -> THERMOCOUPLE + I2C */
#define HAL_ENABLE_MAX6675
#define HAL_ENABLE_DS18B20      /* -> ONEWIRE */
#define HAL_ENABLE_EXTERNAL_ADC /* -> I2C */
#define HAL_ENABLE_GPS          /* -> SWSERIAL */
#define HAL_ENABLE_PWM_FREQ
#define HAL_ENABLE_RGB_LED
#define HAL_ENABLE_ILI9341 /* -> TFT + DISPLAY */
#define HAL_DISPLAY_ILI9341
#define HAL_ENABLE_SSD1306
#define HAL_ENABLE_CRYPTO
#define HAL_ENABLE_CJSON
#define HAL_ENABLE_UNITY
