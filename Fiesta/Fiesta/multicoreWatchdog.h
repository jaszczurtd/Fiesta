
#ifndef T_WATCHDOG
#define T_WATCHDOG

#include <arduino-timer.h>
#include <atomic> 
#include <stdint.h>
#include <arduino-timer.h>

#include <tools.h>
#include "config.h"
#include "start.h"

void setupWatchdog(Timer<> *timer);
void updateWatchdogCore0(void);
void updateWatchdogCore1(void);

void setStartedCore0(void);
void setStartedCore1(void);
bool isEnvironmentStarted(void);

void triggerSystemReset(void);

#endif
