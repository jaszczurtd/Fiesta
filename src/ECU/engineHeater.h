#ifndef T_HEATER
#define T_HEATER

#include <tools_c.h>

#include "config.h"
#include "sensors.h"
#include "tests.h"
#include "engineFan.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool heaterLoEnabled;
  bool heaterHiEnabled;
  bool lastHeaterLoEnabled;
  bool lastHeaterHiEnabled;
} engineHeater;

/**
 * @brief Initialize the engine heater control state.
 * @param self Heater controller instance to initialize.
 * @return None.
 */
void engineHeater_init(engineHeater *self);

/**
 * @brief Update heater outputs from coolant, voltage, fan, glow plug, and RPM state.
 * @param self Heater controller instance to process.
 * @return None.
 */
void engineHeater_process(engineHeater *self);

/**
 * @brief Print the current heater state for diagnostics.
 * @param self Heater controller instance to report.
 * @return None.
 */
void engineHeater_showDebug(engineHeater *self);

/**
 * @brief Drive one heater output level.
 * @param self Heater controller instance issuing the command.
 * @param enable True to enable the selected heater output, false to disable it.
 * @param level PCF8574 output identifier for the heater stage.
 * @return None.
 */
void engineHeater_heater(engineHeater *self, bool enable, int32_t level);

/**
 * @brief Get the shared heater controller instance from ECU context.
 * @return Pointer to the global heater controller instance.
 */
engineHeater *getHeaterInstance(void);

/**
 * @brief Create and initialize the shared heater controller instance.
 * @return None.
 */
void createHeater(void);

#ifdef __cplusplus
}
#endif

#endif
