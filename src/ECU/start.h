
#ifndef T_START
#define T_START

#include <libConfig.h>
#include "config.h"

#include <tools.h>

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
#include "dtcManager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIUM_TIME_ONE_SECOND_DIVIDER 12
#define FREQUENT_TIME_ONE_SECOND_DIVIDER 16

void callAtEverySecond(void);

void initialization(void);
void initialization1(void);
void looper(void);
void looper1(void);
bool seriousAlertSwitch(void);
bool alertSwitch(void);

#ifdef __cplusplus
}
#endif

#endif
