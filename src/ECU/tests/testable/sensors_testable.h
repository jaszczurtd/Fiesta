#ifndef ECU_TESTABLE_SENSORS_H
#define ECU_TESTABLE_SENSORS_H

#include <stdint.h>

#ifdef UNIT_TEST
#ifdef __cplusplus
extern "C" {
#endif

int32_t sensors_computeThrottlePositionFromRaw(int32_t rawVal);
int32_t sensors_calculateEngineLoadFromValues(float pressureBar, float rpm);

#ifdef __cplusplus
}
#endif
#endif

#endif
