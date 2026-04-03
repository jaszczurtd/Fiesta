#ifndef T_TURBO
#define T_TURBO

#include <tools.h>
#include <float.h>

#include "config.h"
#include "start.h"
#include "canDefinitions.h"
#include "rpm.h"
#include "hardwareConfig.h"
#include "tests.h"

#include "engineMaps.h"

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

#endif
