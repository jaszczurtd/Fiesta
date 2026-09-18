#ifndef T_HEATER
#define T_HEATER

#include <JaszczurHAL.h>

#include "config.h"
#include "engineFan.h"
#include "sensors.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool heaterLoEnabled;
  bool heaterHiEnabled;
  bool lastHeaterLoEnabled;
  bool lastHeaterHiEnabled;
} engineHeater;

void engineHeater_init(engineHeater *self);

/**
 * @brief Update heater outputs from coolant, voltage, fan, glow plug, and RPM
 * state.
 */
void engineHeater_process(engineHeater *self);

/**
 * @brief Print the current heater state for diagnostics.
 */
void engineHeater_showDebug(engineHeater *self);

/**
 * @brief Drive one heater output level.
 * @param self Heater controller instance issuing the command.
 * @param enable True to enable the selected heater output, false to disable it.
 * @param level PCF8574 output identifier for the heater stage.
 */
void engineHeater_heater(engineHeater *self, bool enable, int32_t level);

/**
 * @brief Get the shared heater controller instance from ECU context.
 * @return Pointer to the global heater controller instance.
 */
engineHeater *getHeaterInstance(void);

/**
 * @brief Create and initialize the shared heater controller instance.
 */
void createHeater(void);

#ifdef __cplusplus
}
#endif

#endif
