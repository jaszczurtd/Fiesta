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

#define VP37_CALIBRATION_MAX_PERCENTAGE 60
#define VP37_CALIBRATION_CYCLES 10

#define VP37_PWM_MIN 350
#define VP37_PWM_MAX 600

#define VP37_ADJUST_TIMER 50

void vp37Calibrate(void);
void enableVP37(bool enable);
bool isVP37Enabled(void);
void vp37Process(void);
void idleTask(void);

#endif
