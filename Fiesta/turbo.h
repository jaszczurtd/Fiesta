#ifndef T_TURBO
#define T_TURBO

#include <tools.h>
#include <arduino-timer.h>
#include <float.h>

#include "config.h"
#include "start.h"
#include "canDefinitions.h"
#include "rpm.h"
#include "hardwareConfig.h"
#include "tests.h"
#include "TFTExtension.h"

//#define JUST_TEST_BY_THROTTLE



void turboInit(void);
void turboTestTrigger(void);
void turboTest(void);
void turboMainLoop(void);

#endif