
#ifndef T_START
#define T_START

#include <Arduino.h>
#include <Wire.h>
#include <arduino-timer.h>
#include <SPI.h>
#include <mcp_can.h>
#include <tools.h>
#include <multicoreWatchdog.h>
#include "displayMapper.h"

#include "hardwareConfig.h"
#include "sensors.h"
#include "graphics.h"
#include "config.h"
#include "rpm.h"
#include "turbo.h"
#include "engineFan.h"
#include "engineHeater.h"
#include "heatedWindshield.h"
#include "glowPlugs.h"
#include "engineFuel.h"
#include "can.h"
#include "obd-2.h"
#include "vp37.h"
#include "oil.h"
#include "tests.h"

#define MEDIUM_TIME_ONE_SECOND_DIVIDER 12
#define FREQUENT_TIME_ONE_SECOND_DIVIDER 16

void drawMediumImportanceValues(void);
void drawHighImportanceValues(void);
void drawLowImportanceValues(void);
void triggerDrawHighImportanceValue(bool state);

bool callAtEverySecond(void *argument);
bool callAtEveryHalfSecond(void *argument);
bool callAtEveryHalfHalfSecond(void *argument);

void initialization(void);
void initialization1(void);
void looper(void);
void looper1(void);
bool seriousAlertSwitch(void);
bool alertSwitch(void);

#endif
