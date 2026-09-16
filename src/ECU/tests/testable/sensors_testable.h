#ifndef ECU_TESTABLE_SENSORS_H
#define ECU_TESTABLE_SENSORS_H

#include <stdint.h>

#ifdef UNIT_TEST
#ifdef __cplusplus
extern "C" {
#endif

int32_t sensors_computeThrottlePositionFromRaw(int32_t rawVal);
int32_t sensors_calculateEngineLoadFromValues(float pressureBar, float rpm);
uint32_t sensors_muxSettleUs(void);
uint16_t sensors_adcSampleDelayUs(void);

#ifdef __cplusplus
}
#endif
#endif

#endif
