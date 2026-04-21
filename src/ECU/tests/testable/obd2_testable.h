#ifndef ECU_TESTABLE_OBD2_H
#define ECU_TESTABLE_OBD2_H

#include <stdint.h>
#include "hal/hal_can.h"

#ifdef UNIT_TEST
#ifdef __cplusplus
extern "C" {
#endif

uint8_t stMinToMs(uint8_t stMin);
uint8_t obd_encodeTempByte(float tempC);
hal_can_t obdTestGetCanHandle(void);

#ifdef __cplusplus
}
#endif
#endif

#endif
