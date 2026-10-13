#ifndef C_HARDWAREC
#define C_HARDWAREC

// Pins, buses and addresses come from the pico assembly of device_tree.json;
// the code reads the JH_HW_* macros of the generated jh_hardware.h directly.
// The power stage has the parts and values of the ECU one (type
// fiesta,vp37-power-stage) and its own wiring.
#include "../common/adjustometer_protocol.h"
#include "../common/vp37/vp37_config.h"

// hal_i2c_init() starts the default bus 0.
#if JH_HW_BUS_I2C_INDEX != 0
#error "the bench runs the Adjustometer on I2C bus 0"
#endif
// The Adjustometer firmware answers at the address of its protocol header.
#if JH_HW_NODE_ADJUSTOMETER_ADDRESS != ADJUSTOMETER_I2C_ADDR
#error                                                                         \
    "device_tree.json and adjustometer_protocol.h disagree on the Adjustometer address"
#endif

// hal_display_init() drives the ST7796S on SPI 0.
#if JH_HW_BUS_SPI_INDEX != 0
#error "hal_display_init() drives the ST7796S on SPI 0"
#endif

// RGB565 colour constants
#define ST77XX_BLACK 0x0000
#define ST77XX_WHITE 0xFFFF
#define ST77XX_RED 0xF800
#define ST77XX_GREEN 0x07E0
#define ST77XX_BLUE 0x001F
#define ST77XX_YELLOW 0xFFE0
#define ST77XX_CYAN 0x07FF
#define ST77XX_MAGENTA 0xF81F

#endif
