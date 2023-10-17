#ifndef T_FUEL
#define T_FUEL

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"
#include "TFTExtension.h"

//fuel value read without average calculation
//#define JUST_RAW_FUEL_VAL

#define FUEL_MAX_SAMPLES 128
#define FUEL_INIT_VALUE -1

#define FUEL_MEASUREMENT_TIME_START 5
#define FUEL_MEASUREMENT_TIME_DEST 30

#define FUEL_WIDTH 30
#define FUEL_HEIGHT 30

#define FUEL_GAUGE_HEIGHT (FUEL_HEIGHT - 4)
#define FUEL_GAUGE_WIDTH (SCREEN_W - 118)

#define MIN_FUEL_WIDTH 2

#define FUEL_COLOR 0xfda0

#define FUEL_BOX_COLOR 0xBDF7
#define FUEL_FILL_COLOR 0x9CD3

float readFuel(void);
void initFuelMeasurement(void);

//gauge
int f_getWidth(void);
int f_getBaseX(void);
int f_getGaugePos(void);
void redrawFuel(void);
void drawFuelEmpty(void);
void showFuelAmount(int currentVal, int maxVal);
void drawChangeableFuelContent(int w, int fh, int y);

#endif