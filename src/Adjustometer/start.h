
#ifndef T_START
#define T_START

#include <libConfig.h>
#include "config.h"

#include <tools_c.h>

#include "hardwareConfig.h"
#include "sensors.h"
#include "led.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all core-0 Adjustometer services and peripherals.
 * @return None.
 */
void initialization(void);

/**
 * @brief Initialize the second core used by the Adjustometer firmware.
 * @return None.
 */
void initialization1(void);

/**
 * @brief Run one iteration of the core-0 Adjustometer loop.
 * @return None.
 */
void looper(void);

/**
 * @brief Run one iteration of the core-1 Adjustometer loop.
 * @return None.
 */
void looper1(void);

#ifdef __cplusplus
}
#endif

#endif
