
#ifndef T_RPM
#define T_RPM

#include "utils.h"
#include "config.h"
#include "start.h"

#include <arduino-timer.h>

void initRPMCount(void);
void setMaxRPM(void);
void resetRPMEngine(void);
void stabilizeRPM(void);

#endif
