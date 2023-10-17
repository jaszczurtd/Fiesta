#ifndef T_GFX
#define T_GFX

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <tools.h>

#include "TFTExtension.h"
#include "tempGauge.h"
#include "simpleGauge.h"
#include "start.h"
#include "config.h"
#include "engineFuel.h"
#include "can.h"
#include "sensors.h"
#include "tests.h"
#include "icons.h"

#define VOLTS_OK_COLOR 0x4228
#define VOLTS_LOW_ERROR_COLOR 0xA000

#define C_INIT_VAL 99999;

extern const char *err;

bool softInitDisplay(void *arg);
void initGraphics(void);
void redrawAll(void);
void showLogo(void);

//indicators
void redrawVolts(void);
void showVolts(float volts);

#endif
 