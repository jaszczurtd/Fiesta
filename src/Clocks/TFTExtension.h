#ifndef T_TFT_EXTENSION
#define T_TFT_EXTENSION

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ILI9341.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <tools.h>
#include "hardwareConfig.h"
#include "tempGauge.h"
#include "simpleGauge.h"
#include "pressureGauge.h"
#include "logic.h"
#include "engineFuel.h"
#include "can.h"
#include "icons.h"

#define DISPLAY_TXT_SIZE 32
#define DISPLAY_SOFTINIT_TIME 3500

#define SCREEN_W 320
#define SCREEN_H 240

#define TFT TFTExtension
#define COLOR(c) ILI9341_##c

//colors
#define TEXT_COLOR 0xE73C

#define C_GRAY_DARK 0x4208
#define C_GRAY_MEDIUM 0xA514

#define TEMP_INITIAL_COLOR 0x33b7

#define ICONS_BG_COLOR 0x0841

//drawTextForPressureIndicators
#define BAR_TEXT_X 12
#define BAR_TEXT_Y 71

class TFTExtension : public Adafruit_ILI9341 {
public:
TFTExtension(uint8_t cs, uint8_t dc, uint8_t rst);

void softInit(int d);
void drawImage(int x, int y, int width, int height, int background, unsigned short *pointer);
int textWidth(const char* text);
int textHeight(const char* text);
void printlnFromPreparedText(char *displayTxt);
int prepareText(char *displayTxt, const char *format, ...);
void drawTextForPressureIndicators(int x, int y, const char *format, ...);
void setDisplayDefaultFont(void);
void defaultFontWithPosAndColor(int x, int y, int color);
void setTextSizeOneWithColor(int color);
void sansBoldWithPosAndColor(int x, int y, int color);
void serif9ptWithColor(int color);

};

TFT *initTFT(void);
TFT *returnTFTReference(void);
bool softInitDisplay(void *arg);
void redrawAllGauges(void);

#endif
