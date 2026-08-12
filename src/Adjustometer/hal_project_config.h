#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for the Adjustometer project.
 *
 * Opt-in model (HAL >= 1.6.0): only HAL_ENABLE_* modules listed here are
 * compiled in. Core APIs (GPIO, ADC, timer, micros/millis, watchdog,
 * soft_timer, debug) are always available.
 */

/* ── Modules used by Adjustometer ────────────────────────────────────── */

#define HAL_ENABLE_I2C_SLAVE /* I2C slave register map - ECU link  */
#define HAL_ENABLE_RGB_LED   /* NeoPixel status LED                */
#define HAL_ENABLE_APP_TASK1

/* Native RP system stacks, in bytes. */
#define HAL_RP_CORE0_STACK_SIZE 4096u
#define HAL_RP_CORE1_STACK_SIZE 4096u
