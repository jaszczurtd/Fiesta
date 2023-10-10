#ifndef T_DISPLAY_MAPPER
#define T_DISPLAY_MAPPER

#include "config.h"

#define TFT_CS     4 //CS
#define TFT_RST    -1 //reset
#define TFT_DC     3 //A0

#define DISPLAY_SOFTINIT_TIME 1500

#ifdef D_ILI9341
// Hardware-specific library for display
#include "Adafruit_ILI9341.h"

#define SCREEN_W 320
#define SCREEN_H 240

#define DTYPE ILI9341
#define initDisplay() tft.begin()
#define TFT Adafruit_ILI9341
#define COLOR(c) ILI9341_##c

#else
#include <Adafruit_ST7735.h>

#define SCREEN_W 160
#define SCREEN_H 128

#define DTYPE ST7735
#define initDisplay() tft.initR(INITR_BLACKTAB)      // Init ST7735S chip, black tab
#define TFT Adafruit_ST7735
#define COLOR(c) ST7735_##c

#endif

#endif
