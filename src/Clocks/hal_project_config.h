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
#define HAL_ENABLE_MCP9600  /* MCP9600 -> THERMOCOUPLE, I2C       */
#define HAL_ENABLE_GPS
#define HAL_ENABLE_SWSERIAL
#define HAL_GPS_TRANSPORT_SWSERIAL /* GP27/28 have no matching hardware UART.  \
                                    */
#define HAL_ENABLE_CRYPTO          /* hal_crypto + hal_sc_auth (SC link) */
#define HAL_ENABLE_SERIAL_COMMANDS
#define HAL_COMMAND_ROUTER_MAX_COMMANDS 16u
#ifndef HAL_ENABLE_APP_TASK1
#define HAL_ENABLE_APP_TASK1
#endif

/* A missing or stuck MCP9600 must not stall the display core for the default
 * 100 ms per I2C phase; a register read takes well under 1 ms at 100 kHz. */
#define HAL_RP_I2C_TIMEOUT_US 5000u

/* The TFT shares SPI0 with the MCP2515: fills and bitmaps release the bus
 * after every 256 bytes of pixels (about 85 us at 24 MHz), so core 0 can read
 * CAN frames between two bands. */
#define HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES 256u

#define JH_PROJECT_VARIANTS(X)                                                 \
  X(TIMING, "Clocks with CAN, gauge and display timing statistics",            \
    CLOCKS_TIMING_DIAG = 1)

/* Native RP system stacks, in bytes. */
#define HAL_RP_CORE0_STACK_SIZE 4096u
#define HAL_RP_CORE1_STACK_SIZE 4096u
