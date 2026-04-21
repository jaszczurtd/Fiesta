
#ifndef T_START
#define T_START

#include <libConfig.h>
#include "config.h"

#include <tools_c.h>

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

/**
 * @brief Execute once-per-second background tasks.
 * @return None.
 */
void callAtEverySecond(void);

/**
 * @brief Initialize all core-0 ECU services and modules.
 * @return None.
 */
void initialization(void);

/**
 * @brief Initialize core-1 services used by the split runtime.
 * @return None.
 */
void initialization1(void);

/**
 * @brief Run one iteration of the core-0 main loop.
 * @return None.
 */
void looper(void);

/**
 * @brief Run one iteration of the core-1 main loop.
 * @return None.
 */
void looper1(void);

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
