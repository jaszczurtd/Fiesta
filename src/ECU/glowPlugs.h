#ifndef T_GLOWPLUGS
#define T_GLOWPLUGS

#include <tools_c.h>

#include "config.h"
#include "../common/canDefinitions/canDefinitions.h"
#include "sensors.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int32_t glowPlugsTime;
  int32_t glowPlugsLampTime;
  int32_t lastGlowPlugsTime;
  int32_t lastGlowPlugsLampTime;

  unsigned long lastSecond;
  bool warmAfterStart;
  bool initialized;
} glowPlugs;

/**
 * @brief Initialize the glow plug controller state.
 * @param self Glow plug controller instance to initialize.
 * @return None.
 */
void glowPlugs_init(glowPlugs *self);

/**
 * @brief Update glow plug and lamp timing from current engine conditions.
 * @param self Glow plug controller instance to process.
 * @return None.
 */
void glowPlugs_process(glowPlugs *self);

/**
 * @brief Print the current glow plug state for diagnostics.
 * @param self Glow plug controller instance to report.
 * @return None.
 */
void glowPlugs_showDebug(const glowPlugs *self);

/**
 * @brief Drive the glow plug output relay.
 * @param self Glow plug controller instance issuing the command.
 * @param enable True to enable glow plugs, false to disable them.
 * @return None.
 */
void glowPlugs_enableGlowPlugs(glowPlugs *self, bool enable);

/**
 * @brief Drive the glow plug indicator lamp output.
 * @param self Glow plug controller instance issuing the command.
 * @param enable True to enable the lamp, false to disable it.
 * @return None.
 */
void glowPlugs_glowPlugsLamp(glowPlugs *self, bool enable);

/**
 * @brief Check whether glow plugs are currently considered active.
 * @param self Glow plug controller instance to inspect.
 * @return True when heating time is still active, otherwise false.
 */
bool glowPlugs_isGlowPlugsHeating(const glowPlugs *self);

/**
 * @brief Initialize glow plug timers from the current coolant temperature.
 * @param self Glow plug controller instance to update.
 * @param temp Coolant temperature used to derive heating times.
 * @return None.
 */
void glowPlugs_initGlowPlugsTime(glowPlugs *self, float temp);

/**
 * @brief Get the shared glow plug controller instance from ECU context.
 * @return Pointer to the global glow plug controller instance.
 */
glowPlugs *getGlowPlugsInstance(void);

/**
 * @brief Create and initialize the shared glow plug controller instance.
 * @return None.
 */
void createGlowPlugs(void);

#ifdef __cplusplus
}
#endif

#endif
