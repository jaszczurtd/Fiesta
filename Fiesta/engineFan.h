#ifndef T_FAN
#define T_FAN

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

#define FAN_REASON_NONE     0x00
#define FAN_REASON_COOLANT  0x01
#define FAN_REASON_AIR      0x02

bool isFanEnabled(void);
void fanMainLoop(void);
void fan(bool enable);

#endif