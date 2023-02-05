#ifndef T_FAN
#define T_FAN

#include "utils.h"
#include "config.h"
#include "start.h"
#include "sensors.h"

bool isFanEnabled(void);
void fanMainLoop(void);
void fan(bool enable);

#endif