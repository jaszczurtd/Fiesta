#ifndef T_TURBO
#define T_TURBO

#include <tools.h>
#include <arduino-timer.h>

#include "config.h"
#include "start.h"
#include "rpm.h"
#include "hardwareConfig.h"
#include "tests.h"
#include "TFTExtension.h"

//#define JUST_TEST_BY_THROTTLE

#define TURBO_PID_TIME_UPDATE 20.0
#define TURBO_PID_KP 0.4
#define TURBO_PID_KI 0.1
#define TURBO_PID_KD 0.01

void turboInit(void);
void turboTest(void);
void turboMainLoop(void);

#endif