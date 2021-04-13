#ifndef T_INDICATORS
#define T_INDICATORS

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Fonts/FreeSansBold9pt7b.h>

#include "utils.h"
#include "graphics.h"
#include "start.h"

#define TEMP_DOT_X 17
#define TEMP_DOT_Y 39

#define OIL_DOT_X 15
#define OIL_DOT_Y 39

#define BAR_TEXT_X 6
#define BAR_TEXT_Y 51

#define MINIMUM_FUEL_AMOUNT_PERCENTAGE 10

#define OFFSET 4

#define C_INIT_VAL 99999;

//indicators
void redrawTemperature(void);
void showTemperatureAmount(int currentVal, int maxVal);
void redrawOil(void);
void showOilAmount(int currentVal, int maxVal);
void redrawPressure(void);
void showPressureAmount(float current);
void redrawIntercooler(void);
void showICTemperatureAmount(unsigned char currentVal);
void redrawFuel(void);
void drawFuelEmpty(void);
void showFuelAmount(int currentVal, int maxVal);
void drawChangeableFuelContent(int w);
void redrawEngineLoad(void);
void showEngineLoadAmount(unsigned char currentVal);
void redrawRPM(void);
void showRPMamount(int currentVal);
void redrawEGT(void);
void showEGTTemperatureAmount(int currentVal);

//readers
float readCoolantTemp(void);
float readOilTemp(void);

#endif
