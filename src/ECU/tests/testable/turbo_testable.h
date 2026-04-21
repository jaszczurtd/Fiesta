#ifndef ECU_TESTABLE_TURBO_H
#define ECU_TESTABLE_TURBO_H

#include "turbo.h"

#ifdef UNIT_TEST
int32_t Turbo_correctPressureFactor(Turbo *self);
#endif

#endif