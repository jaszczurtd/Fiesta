#ifndef T_FUEL
#define T_FUEL

#include <tools.h>

#include "config.h"
#include "start.h"
#include "graphics.h"
#include "sensors.h"
#include "tests.h"

#define FUEL_MAX_SAMPLES 128
#define FUEL_INIT_VALUE -1

#define FUEL_MEASUREMENT_TIME_START 3
#define FUEL_MEASUREMENT_TIME_DEST 30

#define FUEL_WIDTH 30
#define FUEL_HEIGHT 30

#define FUEL_GAUGE_HEIGHT (FUEL_HEIGHT - 4)
#define FUEL_GAUGE_WIDTH (SCREEN_W - 118)

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