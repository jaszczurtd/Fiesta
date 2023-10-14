#ifndef T_GFX
#define T_GFX

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <tools.h>

#include "TFTExtension.h"
#include "tempGauge.h"
#include "start.h"
#include "config.h"
#include "engineFuel.h"
#include "can.h"
#include "sensors.h"
#include "tests.h"
#include "icons.h"

//colors
#define TEXT_COLOR 0xE73C

#define C_GRAY_DARK 0x4208
#define C_GRAY_MEDIUM 0xA514

#define TEMP_INITIAL_COLOR 0x33b7

#define ICONS_BG_COLOR 0x0841
#define FUEL_COLOR 0xfda0

#define FUEL_BOX_COLOR 0xBDF7
#define FUEL_FILL_COLOR 0x9CD3

#define VOLTS_OK_COLOR 0x4228
#define VOLTS_LOW_ERROR_COLOR 0xA000

#define OFFSET (SCREEN_W / 40)

#define C_INIT_VAL 99999;

#define BAR_TEXT_X 12
#define BAR_TEXT_Y 71

#define VOLTS_MIN_VAL 11.6
#define VOLTS_MAX_VAL 14.7

extern const char *err;

#define MODE_M_NORMAL 0
#define MODE_M_TEMP 1
#define MODE_M_KILOMETERS 2

TFT returnTFTReference(void);
bool softInitDisplay(void *arg);
void initGraphics(void);
void redrawAll(void);
void showLogo(void);
void fillScreenWithColor(int c);
void drawImage(int x, int y, int width, int height, int background, unsigned short *pointer);
int textWidth(const char* text);
int textHeight(const char* text);
void setDisplayDefaultFont(void);
int drawTextForMiddleIcons(int x, int y, int offset, int color, int mode, const char *format, ...);
void drawTextForPressureIndicators(int x, int y, const char *format, ...);
void drawTempValue(int x, int y, int valToDisplay);
void displayErrorWithMessage(int x, int y, const char *msg);
int currentValToHeight(int currentVal, int maxVal);
int prepareText(const char *format, ...);
const char *getPreparedText(void);

//indicators
void redrawIntercooler(void);
void showICTemperatureAmount(int currentVal);
void redrawEngineLoad(void);
void showEngineLoadAmount(int currentVal);
void redrawEGT(void);
void showEGTTemperatureAmount(void);
void redrawVolts(void);
void showVolts(float volts);

bool changeEGT(void *argument);


#endif
 