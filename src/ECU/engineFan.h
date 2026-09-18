#ifndef T_FAN
#define T_FAN

#include <JaszczurHAL.h>

#include "../common/canDefinitions/canDefinitions.h"
#include "config.h"
#include "sensors.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { FAN_REASON_NONE, FAN_REASON_COOLANT, FAN_REASON_AIR };

typedef struct {
  int32_t fanEnabled;
  int32_t lastFanStatus;
} engineFan;

void engineFan_init(engineFan *self);

/**
 * @brief Update fan state from current sensor values and apply output changes.
 */
void engineFan_process(engineFan *self);

/**
 * @brief Print the current fan state for diagnostics.
 */
void engineFan_showDebug(const engineFan *self);

/**
 * @brief Check whether the fan is currently enabled for any reason.
 * @return True when the fan is enabled, otherwise false.
 */
bool engineFan_isFanEnabled(const engineFan *self);

/**
 * @brief Drive the physical fan output.
 * @param self Fan controller instance issuing the command.
 * @param enable True to enable the fan output, false to disable it.
 */
void engineFan_fan(engineFan *self, bool enable);

/**
 * @brief Get the shared fan controller instance from ECU context.
 * @return Pointer to the global fan controller instance.
 */
engineFan *getFanInstance(void);

/**
 * @brief Create and initialize the shared fan controller instance.
 */
void createFan(void);

#ifdef __cplusplus
}
#endif

#endif
