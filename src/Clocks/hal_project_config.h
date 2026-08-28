#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL module configuration for the Clocks project.
 *
 * Opt-in model (HAL >= 1.6.0): only HAL_ENABLE_* modules listed here are
 * compiled in. Core APIs (GPIO, PWM, SPI, ADC, timer, soft_timer,
 * watchdog, debug) are always available.
 */

/* ── Modules used by Clocks ──────────────────────────────────────────── */

#define HAL_ENABLE_CAN      /* Generic CAN API facade             */
#define HAL_ENABLE_MCP2515  /* MCP2515 CAN backend                */
#define HAL_ENABLE_ILI9341  /* ILI9341 TFT -> TFT, DISPLAY        */
#define HAL_DISPLAY_ILI9341 /* Select ILI9341 as the TFT driver    */
#define HAL_ENABLE_RGB_LED  /* NeoPixel status LED                */
#define HAL_ENABLE_CRYPTO   /* hal_crypto + hal_sc_auth (SC link) */
#define HAL_ENABLE_SERIAL_COMMANDS
#define HAL_COMMAND_ROUTER_MAX_COMMANDS 16u
#ifndef HAL_ENABLE_APP_TASK1
#define HAL_ENABLE_APP_TASK1
#endif

/* Native RP system stacks, in bytes. */
#define HAL_RP_CORE0_STACK_SIZE 4096u
#define HAL_RP_CORE1_STACK_SIZE 4096u
