#ifndef T_FAN
#define T_FAN

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

bool isFanEnabled(void);
void fanMainLoop(void);
void fan(bool enable);

#endif