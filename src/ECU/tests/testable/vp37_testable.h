#ifndef ECU_TESTABLE_VP37_H
#define ECU_TESTABLE_VP37_H

#include "vp37.h"

#include <stddef.h>

#ifdef UNIT_TEST
#ifdef __cplusplus
extern "C" {
#endif

float VP37_strokeTaper(const float *knots, size_t count, float percent);
uint32_t VP37_snapshotBegin(const VP37Pump *self);
bool VP37_snapshotEnd(const VP37Pump *self, uint32_t sequence);

#ifdef __cplusplus
}
#endif
#endif

#endif
