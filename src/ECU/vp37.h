#ifndef T_VP37
#define T_VP37

#include <tools_c.h>
#include <hal/hal_pid_controller.h>

#include "config.h"
#include "rpm.h"
#include "obd-2.h"
#include "turbo.h"
#include "hardwareConfig.h"
#include "tests.h"

#include "engineMaps.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_INJECTION_PRESSURE 300 //bar

#define VP37_PID_TIME_UPDATE 30.0
#define VP37_PID_KP 0.45
#define VP37_PID_KI 0.18
#define VP37_PID_KD 0.01

//calibration / stabilization values
#define VOLT_PER_PWM 0.0421
#define VOLTAGE_THRESHOLD 0.2
#define PERCENTAGE_ERROR 3.0

#define VP37_OPERATION_DELAY 5 //microseconds

#define STABILITY_ADJUSTOMETER_TAB_SIZE 4
#define MIN_ADJUSTOMETER_VAL 10

//miliseconds
#define VP37_FUEL_TEMP_UPDATE 500
#define VP37_VOLTAGE_UPDATE 10

#define VP37_CALIBRATION_MAX_PERCENTAGE 50
#define VP37_AVERAGE_VALUES_AMOUNT 5

#define VP37_PERCENTAGE_LIMITER 95

#define VP37_PWM_MIN 378
#define VP37_PWM_MAX VP37_PWM_MIN * 2.5

#define VP37_ADJUST_TIMER 200

void measureFuelTemp(void);
void measureVoltage(void);

typedef struct {
  hal_pid_controller_t adjustController;

  bool vp37Initialized;
  int lastThrottle;
  bool calibrationDone;
  int desiredAdjustometer;
  float pwmValue;
  float voltageCorrection;
  int lastPWMval;
  int finalPWM;
  float lastVolts;
  hal_soft_timer_t fuelTempTimer;
  hal_soft_timer_t voltageTimer;
  int adjustStabilityTable[STABILITY_ADJUSTOMETER_TAB_SIZE];
  int VP37_ADJUST_MIN, VP37_ADJUST_MIDDLE, VP37_ADJUST_MAX, VP37_OPERATE_MAX;
} VP37Pump;

void VP37Pump_init(VP37Pump *self);
void VP37Pump_process(VP37Pump *self);
void VP37Pump_enableVP37(VP37Pump *self, bool enable);
bool VP37Pump_isVP37Enabled(VP37Pump *self);
void VP37Pump_showDebug(VP37Pump *self);

#ifdef __cplusplus
}
#endif

#endif
