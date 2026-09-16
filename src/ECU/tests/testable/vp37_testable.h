#ifndef ECU_TESTABLE_VP37_H
#define ECU_TESTABLE_VP37_H

#include <stddef.h>

#ifdef UNIT_TEST
#ifdef __cplusplus
extern "C" {
#endif

float VP37_strokeTaper(const float *knots, size_t count, float percent);

#ifdef __cplusplus
}
#endif
#endif

#endif
