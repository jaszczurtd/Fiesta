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
#include "TFTExtension.h"

#include "engineMaps.h"
#include "EngineController.h"

//#define JUST_TEST_BY_THROTTLE

//#define TURBO_PID_TIME_UPDATE 6.0
//#define TURBO_PID_KP 0.7
//#define TURBO_PID_KI 0.1
//#define TURBO_PID_KD 0.05

#define MIN_TPS 0    // 0%
#define MAX_TPS 100  // 100%

#define TEST_DURATION_MS 1000 
#define PWM_MIN_PERCENT 0
#define PWM_MAX_PERCENT 100 
#define STEP_PERCENT 5
#define UPDATE_INTERVAL_MS 50

#define SOLENOID_UPDATE_TIME 700
#define PRESSURE_LIMITER_FACTOR 2
#define MIN_TEMPERATURE_CORRECTION 30

class Turbo : public EngineController {
private:

  int scaleTurboValues(int value, bool reverse);

  int desiredPWM;
  int engineThrottlePercentageValue;
  int posThrottle;
  bool pedalPressed;
  int pressurePercentage;
  int RPM_index;
  unsigned long lastSolenoidUpdate;

public:
  Turbo();
  void init() override;  
  void process() override;
  int correctPressureFactor(void);
  void showDebug(void);
};

#endif // TURBO_CONTROL_H

