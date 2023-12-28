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

#define VP37_MIN_ADJUSTOMETER_VAL 0
#define VP37_MAX_ADJUSTOMETER_VAL 1024

#define VP37_PWM_MIN 350
#define VP37_PWM_MAX 620

#define VP37_ADJUST_MAX 2000
#define VP37_ADJUST_MIN 4810
#define VP37_ADJUST_TIMER 100

void enableVP37(bool enable);
bool isVP37Enabled(void);
void vp37Process(void);

#endif
