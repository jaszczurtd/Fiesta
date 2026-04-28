#ifndef T_HEATED_WINDSHIELD
#define T_HEATED_WINDSHIELD

#include <tools_c.h>

#include "config.h"
#include "../common/canDefinitions/canDefinitions.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool heatedWindowEnabled;
  bool lastHeatedWindowEnabled;
  bool waitingForUnpress;

  int32_t heatedWindowsOverallTimer;
  unsigned long lastHeatedWindowsSecond;
} heatedWindshields;

/**
 * @brief Initialize heated windshield control state and input pin mode.
 * @param self Heated windshield controller instance to initialize.
 * @return None.
 */
void heatedWindshields_init(heatedWindshields *self);

/**
 * @brief Update heated windshield state from button input, timer, and voltage.
 * @param self Heated windshield controller instance to process.
 * @return None.
 */
void heatedWindshields_process(heatedWindshields *self);

/**
 * @brief Print the current heated windshield state for diagnostics.
 * @param self Heated windshield controller instance to report.
 * @return None.
 */
void heatedWindshields_showDebug(heatedWindshields *self);

/**
 * @brief Drive one heated windshield output channel.
 * @param self Heated windshield controller instance issuing the command.
 * @param enable True to enable the selected side, false to disable it.
 * @param side PCF8574 output identifier for the requested side.
 * @return None.
 */
void heatedWindshields_heatedWindow(heatedWindshields *self, bool enable, int32_t side);

/**
 * @brief Check whether heated windshield mode is currently active.
 * @param self Heated windshield controller instance to inspect.
 * @return True when heating is enabled, otherwise false.
 */
bool heatedWindshields_isHeatedWindowEnabled(const heatedWindshields *self);

/**
 * @brief Get the shared heated windshield controller instance from ECU context.
 * @return Pointer to the global heated windshield controller instance.
 */
heatedWindshields *getHeatedWindshieldsInstance(void);

/**
 * @brief Create and initialize the shared heated windshield controller instance.
 * @return None.
 */
void createHeatedWindshields(void);

#ifdef __cplusplus
}
#endif

#endif
