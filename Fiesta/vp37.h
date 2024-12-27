#ifndef T_VP37
#define T_VP37

#include <tools.h>
#include <arduino-timer.h>
#include <pidController.h>

#include "config.h"
#include "rpm.h"
#include "obd-2.h"
#include "hardwareConfig.h"
#include "tests.h"

#include "engineMaps.h"
#include "EngineController.h"

#define STABILITY_ADJUSTOMETER_TAB_SIZE 6
#define DEFAULT_INJECTION_PRESSURE 300 //bar

bool measureFuelTemp(void *arg);
bool measureVoltage(void *arg);

class VP37Pump : public EngineController {
private:
  PIDController *adjustController;

  bool vp37Initialized;
  bool calibrationDone;
  int lastPWMval;
  int finalPWM;
  int desiredAdjustometer;
  int vp37AdjustMin, vp37AdjustMiddle, vp37AdjustMax;
  int pidErr;
 
  int currentAdjustometerPosition;

  int getMaxAdjustometerPWMVal(void);
  int getAdjustometerStable(void);
  int makeCalibrationValue(void);
  void makeVP37Calibration(void);
  void applyDelay(void);

public:
  VP37Pump();
  ~VP37Pump();
  void init() override;  
  void process() override;
  void enableVP37(bool enable);
  bool isVP37Enabled(void);
  void VP37TickMainTimer(void);
  void showDebug(void);
  void setInjectionTiming(int angle);
  void updateVP37AdjustometerPosition(void);
  void setVP37PID(float kp, float ki, float kd, bool shouldTriggerReset);
  void getVP37PIDValues(float *kp, float *ki, float *kd);
  int getVP37PIDTimeUpdate(void);

  void setVP37Throttle(int accel);
  int getMinVP37ThrottleValue(void);
  int getMaxVP37ThrottleValue(void);

};

#endif
