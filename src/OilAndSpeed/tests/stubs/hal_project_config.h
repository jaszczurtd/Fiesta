#pragma once

/* Test override for OilAndSpeed HAL configuration.
 * Keep module exclusions, but do not disable Unity for unit tests.
 */
#define HAL_DISABLE_WIFI
#define HAL_DISABLE_EEPROM
#define HAL_DISABLE_GPS
#define HAL_DISABLE_UART
#define HAL_DISABLE_SWSERIAL
#define HAL_DISABLE_DISPLAY
#define HAL_DISABLE_MAX6675
#define HAL_DISABLE_EXTERNAL_ADC
#define HAL_ENABLE_CRYPTO  /* config.cpp uses hal_base64_encode for SC_GET_META */
