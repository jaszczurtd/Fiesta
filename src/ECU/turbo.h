#ifndef T_TURBO
#define T_TURBO

#include <tools_c.h>
#include <float.h>

#include "config.h"
#include "../common/canDefinitions/canDefinitions.h"
#include "rpm.h"
#include "hardwareConfig.h"
#include "tests.h"

#include "engineMaps.h"

#ifdef __cplusplus
extern "C" {
#endif

//#define JUST_TEST_BY_THROTTLE

#define SOLENOID_UPDATE_TIME 700
#define PRESSURE_LIMITER_FACTOR 2
#define MIN_TEMPERATURE_CORRECTION 30
#define TURBO_DEBUG_UPDATE 1500

typedef struct {
  unsigned long lastSolenoidUpdate;

  int32_t engineThrottlePercentageValue;
  int32_t posThrottle;
  int32_t n75;
  int32_t RPM_index;

  int32_t lastThrottlePos;
  int32_t lastPosThrottle;
  bool lastPedalPressed;
  int32_t lastRPM_index;
  int32_t lastPressurePercentage;
  int32_t lastN75;
} Turbo;

/**
 * @brief Initialize turbo control state.
 * @param self Turbo controller instance to initialize.
 * @return None.
 */
void Turbo_init(Turbo *self);

/**
 * @brief Update turbo actuator command from legacy driver-demand, RPM, and boost state.
 * @param self Turbo controller instance to process.
 * @return None.
 * @note The `n75` field intentionally follows OEM N75 terminology for the boost-control
 *       solenoid path. The demand input still comes from legacy throttle-named driver
 *       demand rather than from a final allowed-fuel-quantity variable.
 */
void Turbo_process(Turbo *self);

/**
 * @brief Run the placeholder turbo test path.
 * @param self Turbo controller instance to exercise.
 * @return None.
 */
void Turbo_turboTest(Turbo *self);

/**
 * @brief Print selected turbo controller values for diagnostics.
 * @param self Turbo controller instance to report.
 * @return None.
 */
void Turbo_showDebug(Turbo *self);

#ifdef __cplusplus
}
#endif

#endif
