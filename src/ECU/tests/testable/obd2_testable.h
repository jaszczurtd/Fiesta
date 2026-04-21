#ifndef ECU_TESTABLE_OBD2_H
#define ECU_TESTABLE_OBD2_H

#include <stdint.h>

#ifdef UNIT_TEST
uint8_t stMinToMs(uint8_t stMin);
uint8_t obd_encodeTempByte(float tempC);
#endif

#endif