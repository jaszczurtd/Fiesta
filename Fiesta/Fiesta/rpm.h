
#ifndef T_RPM
#define T_RPM

#include <arduino-timer.h>

#include "utils.h"
#include "config.h"
#include "start.h"
#include "sensors.h"

#define RPM_CORRECTION_VAL 50

void initRPMCount(void);
void setMaxRPM(void);
void resetRPMEngine(void);
void stabilizeRPM(void);

#endif
