#ifndef ECU_TESTABLE_DTC_MANAGER_H
#define ECU_TESTABLE_DTC_MANAGER_H

#include <stdint.h>

#ifdef UNIT_TEST
int findDtcIndex(uint16_t code);
uint16_t dtcKvEffectiveSpan(void);
#endif

#endif