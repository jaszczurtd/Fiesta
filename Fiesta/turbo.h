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

#define MIN_TPS 0    // 0%
#define MAX_TPS 100  // 100%

#define TEST_DURATION_MS 1000 
#define PWM_MIN_PERCENT 0
#define PWM_MAX_PERCENT 100 
#define STEP_PERCENT 5
#define UPDATE_INTERVAL_MS 50

class Turbo : public EngineController {
private:

  PIDController *turboController;
  float minBoost, maxBoost;
  int lastTurboPWM;
  bool turboInitialized;

  int getRPMIndex(int rpm);
  int getTPSIndex(int tps);
  float getBoostPressure(int rpm, int tps);
  int scaleTurboValues(float value, bool reverse);

public:
  Turbo();
  void init() override;  
  void process() override;
  void turboTest(void);
};

#endif // TURBO_CONTROL_H

