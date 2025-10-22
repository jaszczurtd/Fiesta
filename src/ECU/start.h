
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

#include "hardwareConfig.h"
#include "sensors.h"
#include "rpm.h"
#include "turbo.h"
#include "engineFan.h"
#include "engineHeater.h"
#include "heatedWindshield.h"
#include "glowPlugs.h"
#include "can.h"
#include "obd-2.h"
#include "vp37.h"
#include "gps.h"
#include "engineFuel.h"
#include "tests.h"

#define MEDIUM_TIME_ONE_SECOND_DIVIDER 12
#define FREQUENT_TIME_ONE_SECOND_DIVIDER 16

bool callAtEverySecond(void *arg);

void initialization(void);
void initialization1(void);
void looper(void);
void looper1(void);
bool seriousAlertSwitch(void);
bool alertSwitch(void);

#endif
