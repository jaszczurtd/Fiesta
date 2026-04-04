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

typedef struct {
  unsigned long lastSolenoidUpdate;

  int engineThrottlePercentageValue;
  int posThrottle;
  int n75;
  int RPM_index;

  int lastThrottlePos;
  int lastPosThrottle;
  bool lastPedalPressed;
  int lastRPM_index;
  int lastPressurePercentage;
  int lastN75;
} Turbo;

void Turbo_init(Turbo *self);
void Turbo_process(Turbo *self);
void Turbo_turboTest(Turbo *self);
void Turbo_showDebug(Turbo *self);

#ifdef __cplusplus
}
#endif

#endif
