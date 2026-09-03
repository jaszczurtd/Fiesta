
#ifndef T_START
#define T_START

#include "config.h"
#include <libConfig.h>

#include <JaszczurHAL.h>

#include "can.h"
#include "dtcManager.h"
#include "engineFan.h"
#include "engineFuel.h"
#include "engineHeater.h"
#include "engine_operation.h"
#include "glowPlugs.h"
#include "gps.h"
#include "hardwareConfig.h"
#include "heatedWindshield.h"
#include "obd-2.h"
#include "rpm.h"
#include "sensors.h"
#include "tests.h"
#include "turbo.h"
#include "vp37.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIUM_TIME_ONE_SECOND_DIVIDER 12
#define FREQUENT_TIME_ONE_SECOND_DIVIDER 16

/**
 * @brief Execute once-per-second background tasks.
 * @return None.
 */
void callAtEverySecond(void);

/**
 * @brief Reserved legacy severe-alert input helper.
 * @return Alert state when implemented.
 */
bool seriousAlertSwitch(void);

/**
 * @brief Reserved legacy alert input helper.
 * @return Alert state when implemented.
 */
bool alertSwitch(void);

#ifdef __cplusplus
}
#endif

#endif
