#ifndef T_TURBO
#define T_TURBO

#include <tools.h>
#include <arduino-timer.h>
#include <float.h>
#include <pidController.h>

#include "config.h"
#include "start.h"
#include "canDefinitions.h"
#include "rpm.h"
#include "hardwareConfig.h"
#include "tests.h"

#include "engineMaps.h"
#include "EngineController.h"

//#define JUST_TEST_BY_THROTTLE


#define SOLENOID_UPDATE_TIME 700
#define PRESSURE_LIMITER_FACTOR 2
#define MIN_TEMPERATURE_CORRECTION 30

class Turbo : public EngineController {
private:

  unsigned long lastSolenoidUpdate = 0;

  int scaleTurboValues(int value);
  int correctPressureFactor(void);

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

public:
  Turbo();
  void init() override;  
  void process() override;
  void turboTest(void);
  void showDebug(void);


};

#endif // TURBO_CONTROL_H

