#ifndef T_TURBO
#define T_TURBO

#include <tools.h>
#include <arduino-timer.h>

#include "config.h"
#include "start.h"
#include "rpm.h"
#include "hardwareConfig.h"
#include "tests.h"

#define SOLENOID_UPDATE_TIME 700
#define PRESSURE_LIMITER_FACTOR 2
#define MIN_TEMPERATURE_CORRECTION 30

//#define JUST_TEST_BY_THROTTLE

void turboMainLoop(void);

#define BAR_TEXT_X 12
#define BAR_TEXT_Y 72

void redrawPressure(void);
void showPressureAmount(float current);

#endif