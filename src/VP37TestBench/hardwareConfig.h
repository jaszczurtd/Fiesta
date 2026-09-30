#ifndef C_HARDWAREC
#define C_HARDWAREC

// The power stage is the twin of the ECU one: same pins, same parts, same
// values. Everything the drive needs comes from the shared header.
#include "../common/vp37/vp37_power_stage.h"

// The Adjustometer runs its bus at the fast-mode clock, like on the ECU.
#define PIN_SDA 0
#define PIN_SCL 1

#define PIN_MISO 16
#define PIN_MOSI 19
#define PIN_SCK 18

// Drive enable: a plain GPIO stands in for the ECU's PCF8574 output.
#define PIN_VP37_ENABLE 22

// Board status LED. The firmware gets the pin from the board profile; the
// host mock has no board, so the Pico pin is named explicitly.
#ifdef HAL_LED_BUILTIN
#define PIN_ONBOARD_LED HAL_LED_BUILTIN
#else
#define PIN_ONBOARD_LED 25
#endif

// LCD / display
#define TFT_CS 17  // CS
#define TFT_RST 20 // reset
#define TFT_DC 15  // A0

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
