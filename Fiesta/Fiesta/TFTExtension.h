#ifndef T_TFT_EXTENSION
#define T_TFT_EXTENSION

#include <Adafruit_ILI9341.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include "hardwareConfig.h"

#define DISPLAY_SOFTINIT_TIME 1500

#define SCREEN_W 320
#define SCREEN_H 240

#define initDisplay() tft.begin()
#define TFT TFTExtension
#define COLOR(c) ILI9341_##c

class TFTExtension : public Adafruit_ILI9341 {
public:
TFTExtension(uint8_t cs, uint8_t dc, uint8_t rst);

void softInit(int d);
void drawImage(int x, int y, int width, int height, int background, unsigned short *pointer);
int textWidth(const char* text);
int textHeight(const char* text);
void setDisplayDefaultFont(void);
void defaultFontWithPosAndColor(int x, int y, int color);
void setTextSizeOneWithColor(int color);
void sansBoldWithPosAndColor(int x, int y, int color);
void serif9ptWithColor(int color);

};

#endif
