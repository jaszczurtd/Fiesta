#ifndef T_VP37
#define T_VP37

#include <tools.h>
#include <arduino-timer.h>
#include <pidController.h>

#include "config.h"
#include "start.h"
#include "rpm.h"
#include "obd-2.h"
#include "turbo.h"
#include "hardwareConfig.h"
#include "tests.h"

#include "engineMaps.h"
#include "EngineController.h"

#define DEFAULT_INJECTION_PRESSURE 300 //bar

#define PERCENTAGE_ERROR 3.0

#define VP37_OPERATION_DELAY 5 //microseconds

#define STABILITY_ADJUSTOMETER_TAB_SIZE 4
#define MIN_ADJUSTOMETER_VAL 10

//miliseconds
#define VP37_FUEL_TEMP_UPDATE 500
#define VP37_VOLTAGE_UPDATE 8

#define VP37_CALIBRATION_MAX_PERCENTAGE 50
#define VP37_AVERAGE_VALUES_AMOUNT 5

#define VP37_PERCENTAGE_LIMITER 95

#define VP37_PWM_MIN 378
#define VP37_PWM_BASE_FACTOR 2.5 //how many times PWM_MAX should be bigger than PWM_MIN
#define VP37_TEMP_LIMIT_FACTOR 3.0 //limit (tweakable) for calculating PWM_MAX

#define VP37_ADJUST_TIMER 200

bool measureFuelTemp(void *arg);
bool measureVoltage(void *arg);

class VP37Pump : public EngineController {
private:
  PIDController *adjustController;

  bool vp37Initialized;
  int lastThrottle;
  bool calibrationDone;
  int desiredAdjustometer;
  float voltageCorrection;
  int lastPWMval;
  int finalPWM;
  int adjustStabilityTable[STABILITY_ADJUSTOMETER_TAB_SIZE];
  int VP37_ADJUST_MIN, VP37_ADJUST_MIDDLE, VP37_ADJUST_MAX, VP37_OPERATE_MAX;

  int getMaxAdjustometerPWMVal(void);
  int getAdjustometerStable(void);
  int makeCalibrationValue(void);
  float getCalibrationError(int from);
  bool isInRangeOf(float desired, float val);
  void throttleCycle(void);
  void initVP37(void);
  float calculateVP37PWMmax(float temperature);

public:
  VP37Pump();
  void init() override;  
  void process() override;
  void enableVP37(bool enable);
  bool isVP37Enabled(void);
  void showDebug(void);
};

#endif
