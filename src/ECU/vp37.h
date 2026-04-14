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

#define VP37_PID_TIME_UPDATE 45.0
#define VP37_PID_KP 0.45f
#define VP37_PID_KI 0.08f
#define VP37_PID_KD 0.01f
#define VP37_PID_TF 0.032f

// calibration / stabilization values
#define VOLT_PER_PWM 0.0421
#define VOLTAGE_THRESHOLD 0.2
#define PERCENTAGE_ERROR 3.0

#define VP37_OPERATION_DELAY 5 //microseconds

#define STABILITY_ADJUSTOMETER_TAB_SIZE 8
#define MIN_ADJUSTOMETER_VAL 10

//miliseconds
#define VP37_FUEL_TEMP_UPDATE 500
#define VP37_VOLTAGE_UPDATE 10

#define VP37_CALIBRATION_MAX_PERCENTAGE 50
#define VP37_AVERAGE_VALUES_AMOUNT 5

#define VP37_PWM_MIN 378
#define VP37_PWM_MAX (VP37_PWM_MIN * 4.0)

#define VP37_ADJUST_TIMER 200

#define VP37_ACCELERATION_MIN 0
#define VP37_ACCELERATION_MAX 100

// Ramp-down step per cycle (in throttle percentage units).
// Higher = faster descent. 0.5 = smooth, 5+ = snappy.
#define VP37_THROTTLE_RAMP_DOWN_STEP 0.9f

#define TIMING_PWM_MIN 0
#define TIMING_PWM_MAX PWM_RESOLUTION

void measureFuelTemp(void);
void measureVoltage(void);

typedef struct {
  hal_pid_controller_t adjustController;

  bool vp37Initialized;
  float lastThrottle;
  bool calibrationDone;
  int32_t desiredAdjustometer;
  int32_t currentAdjustometerPosition;
  int32_t pidErr;
  float pwmValue;
  float voltageCorrection;
  int32_t lastPWMval;
  int32_t finalPWM;
  float lastVolts;
  hal_soft_timer_t fuelTempTimer;
  hal_soft_timer_t voltageTimer;
  int adjustStabilityTable[STABILITY_ADJUSTOMETER_TAB_SIZE];
  int32_t VP37_ADJUST_MIN, VP37_ADJUST_MIDDLE, VP37_ADJUST_MAX, VP37_OPERATE_MAX;
} VP37Pump;

void VP37_init(VP37Pump *self);
void VP37_process(VP37Pump *self);
void VP37_enableVP37(VP37Pump *self, bool enable);
bool VP37_isVP37Enabled(VP37Pump *self);
void VP37_showDebug(VP37Pump *self);

void VP37_setInjectionTiming(VP37Pump *self, int32_t angle);
void VP37_setVP37Throttle(VP37Pump *self, float accel);
int32_t VP37_getMinVP37ThrottleValue(VP37Pump *self);
int32_t VP37_getMaxVP37ThrottleValue(VP37Pump *self);
void VP37_setVP37PID(VP37Pump *self, float kp, float ki, float kd, bool shouldTriggerReset);
void VP37_getVP37PIDValues(VP37Pump *self, float *kp, float *ki, float *kd);
float VP37_getVP37PIDTimeUpdate(VP37Pump *self);

#ifdef __cplusplus
}
#endif

#endif
