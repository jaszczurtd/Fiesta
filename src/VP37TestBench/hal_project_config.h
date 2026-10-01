#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration of the VP37TestBench firmware.
 *
 * Picked up by hal_config.h via __has_include; only the HAL_ENABLE_* modules
 * listed here are compiled. The bench drives one real VP37 pump through the
 * shared module in src/common/vp37, so it needs the same drive features as
 * the ECU, plus the ST7796S display.
 */

#include "../common/vp37/vp37_hal_config.h"

#define HAL_ENABLE_I2C     /* Adjustometer feedback bus          */
#define HAL_ENABLE_SPI     /* display bus                        */
#define HAL_ENABLE_ST7796S /* 480x320 TFT panel                  */
#define HAL_DISPLAY_ST7796S
#define HAL_ENABLE_PWM_FREQ /* quantity and timing actuators      */
#define HAL_ENABLE_ADC_SCAN /* shunt, demand pot and supply scan  */
// cppcheck-suppress misra-c2012-2.5 ; DR-013: build-system macro
#define HAL_ENABLE_APP_TASK1

/* HAL app-entry mode: app_start / app_task0 / app_task1. */
#ifndef HAL_PROVIDE_APP_ENTRY
#define HAL_PROVIDE_APP_ENTRY
#endif
