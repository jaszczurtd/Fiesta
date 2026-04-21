#ifndef ECU_TESTABLE_DTC_MANAGER_H
#define ECU_TESTABLE_DTC_MANAGER_H

#include <stdint.h>

#ifdef UNIT_TEST
#ifdef __cplusplus
extern "C" {
#endif

int findDtcIndex(uint16_t code);
uint16_t dtcKvEffectiveSpan(void);

#ifdef __cplusplus
}
#endif
#endif

#endif
