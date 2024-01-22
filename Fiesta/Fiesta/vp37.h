#ifndef T_VP37
#define T_VP37

#include <tools.h>
#include <arduino-timer.h>

#include "config.h"
#include "start.h"
#include "rpm.h"
#include "obd-2.h"
#include "turbo.h"
#include "hardwareConfig.h"
#include "tests.h"

typedef struct {
  float dt;
  float last_time;
  float integral;
  float previous;
  float output;
  float kp;
  float ki;
  float kd;
} PIDController;

#define PID_KP 0.6
#define PID_KI 0.50
#define PID_KD 0.001

//calibration / stabilization values
#define VOLT_PER_PWM 0.0192
#define VOLT_MIN_DIFF 0.2
#define PERCENTAGE_ERROR 3.0

#define VP37_OPERATION_DELAY 5 //microseconds

#define MIN_ADJUSTOMETER_VAL 10

//miliseconds
#define VP37_FUEL_TEMP_UPDATE 500
#define VP37_VOLTAGE_UPDATE 10

#define VP37_CALIBRATION_MAX_PERCENTAGE 50
#define VP37_AVERAGE_VALUES_AMOUNT 5

#define VP37_PERCENTAGE_LIMITER 90

#define VP37_PWM_MIN 390

#define VP37_ADJUST_TIMER 200

void vp37Calibrate(void);
void enableVP37(bool enable);
bool isVP37Enabled(void);
void vp37Process(void);
void showVP37Debug(void);

#endif
