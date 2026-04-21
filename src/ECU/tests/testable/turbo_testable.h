#ifndef ECU_TESTABLE_TURBO_H
#define ECU_TESTABLE_TURBO_H

#include "turbo.h"

#ifdef UNIT_TEST
#ifdef __cplusplus
extern "C" {
#endif

int32_t Turbo_correctPressureFactor(Turbo *self);

#ifdef __cplusplus
}
#endif
#endif

#endif
