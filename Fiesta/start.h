
#ifndef T_START
#define T_START

#include <libConfig.h>
#include "config.h"

#include <Arduino.h>

#ifdef INC_FREERTOS_H
#include <FreeRTOS.h>
#include "task.h"
#include "semphr.h"
#endif

#include <Wire.h>
#include <arduino-timer.h>
#include <SPI.h>
#include <mcp_can.h>
#include <tools.h>
#include <multicoreWatchdog.h>

#include "TFTExtension.h"
#include "tempGauge.h"
#include "simpleGauge.h"
#include "pressureGauge.h"
#include "hardwareConfig.h"
#include "sensors.h"
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
#include "gps.h"
#include "tests.h"

#define MEDIUM_TIME_ONE_SECOND_DIVIDER 12
#define FREQUENT_TIME_ONE_SECOND_DIVIDER 16

void drawMediumImportanceValues(void);
void drawHighImportanceValues(void);
void drawLowImportanceValues(void);
void drawHighImportanceValuesIfChanged(void);
void triggerDrawHighImportanceValue(bool state);

bool callAtEverySecond(void *arg);
bool callAtEveryHalfSecond(void *arg);
bool callAtEveryHalfHalfSecond(void *arg);

void initialization(void);
void initialization1(void);
void looper(void);
void looper1(void);
bool seriousAlertSwitch(void);
bool alertSwitch(void);

#endif
