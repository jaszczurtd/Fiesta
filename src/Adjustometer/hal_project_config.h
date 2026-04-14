#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for the Adjustometer project.
 *
 * This file is automatically picked up by hal_config.h via __has_include.
 * Define HAL_DISABLE_* flags here to exclude unused HAL modules from the
 * build.  Dependency propagation (e.g. EEPROM → KV) is handled by
 * hal_config.h — you only need to disable the base module.
 */

/* ── Modules not used by Adjustometer ────────────────────────────────── */

#define HAL_DISABLE_WIFI            /* → propagates HAL_DISABLE_TIME      */
#define HAL_DISABLE_EEPROM          /* → propagates HAL_DISABLE_KV        */
#define HAL_DISABLE_CAN
#define HAL_DISABLE_DISPLAY
#define HAL_DISABLE_RGB_LED
#define HAL_DISABLE_THERMOCOUPLE
#define HAL_DISABLE_UART
#define HAL_DISABLE_SWSERIAL        /* → propagates HAL_DISABLE_GPS       */
#define HAL_DISABLE_I2C             /* master — not needed, using slave    */
#define HAL_DISABLE_PWM_FREQ
#define HAL_DISABLE_UNITY
