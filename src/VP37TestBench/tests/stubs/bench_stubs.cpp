/*
 * VP37TestBench test stubs.
 *
 * The multicoreWatchdog runtime is firmware only (its timers and the second
 * core do not exist on host), so host-unit tests get stubs. The environment
 * flags keep the real behaviour - both cores must report started - because
 * app_task0/app_task1 branch on it.
 */

#include <utils/multicoreWatchdog.h>

static bool s_started_core0 = false;
static bool s_started_core1 = false;

void watchdog_feed(void) {}

bool setupWatchdog(void (*function)(int *values, int size), unsigned int time) {
  (void)function;
  (void)time;
  return true;
}

void updateWatchdogCore0(void) {}
void updateWatchdogCore1(void) {}

void setStartedCore0(void) { s_started_core0 = true; }
void setStartedCore1(void) { s_started_core1 = true; }

bool isEnvironmentStarted(void) { return s_started_core0 && s_started_core1; }
