#ifndef T_FAN
#define T_FAN

#include <tools_c.h>

#include "config.h"
#include "sensors.h"
#include "tests.h"
#include "../common/canDefinitions/canDefinitions.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  FAN_REASON_NONE, FAN_REASON_COOLANT, FAN_REASON_AIR
};

typedef struct {
  int32_t fanEnabled;
  int32_t lastFanStatus;
} engineFan;

/**
 * @brief Initialize the fan control state.
 * @param self Fan controller instance to initialize.
 * @return None.
 */
void engineFan_init(engineFan *self);

/**
 * @brief Update fan state from current sensor values and apply output changes.
 * @param self Fan controller instance to process.
 * @return None.
 */
void engineFan_process(engineFan *self);

/**
 * @brief Print the current fan state for diagnostics.
 * @param self Fan controller instance to report.
 * @return None.
 */
void engineFan_showDebug(const engineFan *self);

/**
 * @brief Check whether the fan is currently enabled for any reason.
 * @param self Fan controller instance to inspect.
 * @return True when the fan is enabled, otherwise false.
 */
bool engineFan_isFanEnabled(const engineFan *self);

/**
 * @brief Drive the physical fan output.
 * @param self Fan controller instance issuing the command.
 * @param enable True to enable the fan output, false to disable it.
 * @return None.
 */
void engineFan_fan(engineFan *self, bool enable);

/**
 * @brief Get the shared fan controller instance from ECU context.
 * @return Pointer to the global fan controller instance.
 */
engineFan *getFanInstance(void);

/**
 * @brief Create and initialize the shared fan controller instance.
 * @return None.
 */
void createFan(void);

#ifdef __cplusplus
}
#endif

#endif
