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

#define PID_TIME_UPDATE 90.0
#define PID_KP 0.45
#define PID_KI 0.28
#define PID_KD 0.001

//calibration / stabilization values
#define VOLT_PER_PWM 0.0421
#define VOLT_MIN_DIFF 0.2
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

#define VP37_PWM_MIN 380
#define VP37_PWM_MAX VP37_PWM_MIN * 2.3

#define VP37_ADJUST_TIMER 200

void vp37Calibrate(void);
void enableVP37(bool enable);
bool isVP37Enabled(void);
void vp37Process(void);
void showVP37Debug(void);

#endif
