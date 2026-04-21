#ifndef ECU_TESTABLE_SENSORS_H
#define ECU_TESTABLE_SENSORS_H

#include <stdint.h>

#ifdef UNIT_TEST
int32_t sensors_computeThrottlePositionFromRaw(int32_t rawVal);
int32_t sensors_calculateEngineLoadFromValues(float pressureBar, float rpm);
#endif

#endif