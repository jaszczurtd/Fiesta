#ifndef T_TURBO
#define T_TURBO

#include <tools_c.h>
#include <float.h>

#include "config.h"
#include "canDefinitions.h"
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

void Turbo_init(Turbo *self);
void Turbo_process(Turbo *self);
void Turbo_turboTest(Turbo *self);
void Turbo_showDebug(Turbo *self);

#ifdef __cplusplus
}
#endif

#endif
