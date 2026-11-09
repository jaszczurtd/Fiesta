#ifndef ECU_DTC_CODES_H
#define ECU_DTC_CODES_H

#include "../common/obdDefinitions/obd_dtc_catalog.h"
#include <stdint.h>

/** @name Project diagnostic trouble codes
 *  Aliases of the shared catalogue in src/common/obdDefinitions: the two-byte
 *  SAE J2012 wire value and the description of every code come from there.
 *  @{ */
#define DTC_OBD_CAN_INIT_FAIL ((uint16_t)OBD_DTC_U1900)
#define DTC_PCF8574_COMM_FAIL ((uint16_t)OBD_DTC_U190D)
#define DTC_CAN_BUS_FAULT ((uint16_t)OBD_DTC_U0073)
#define DTC_PWM_CHANNEL_NOT_INIT ((uint16_t)OBD_DTC_P0657)
#define DTC_DPF_COMM_LOST ((uint16_t)OBD_DTC_U0100)
#define DTC_EGT_COMM_LOST ((uint16_t)OBD_DTC_U1902)
#define DTC_CAN0_INIT_FAIL ((uint16_t)OBD_DTC_U1903)
#define DTC_ISOTP_FC_TIMEOUT ((uint16_t)OBD_DTC_U1906)
#define DTC_ISOTP_FC_ABORT ((uint16_t)OBD_DTC_U1907)
#define DTC_ENGINE_OVERSPEED ((uint16_t)OBD_DTC_P0219)
#define DTC_ECM_EEPROM_FAULT ((uint16_t)OBD_DTC_P062F)
#define DTC_SYSTEM_VOLTAGE_LOW ((uint16_t)OBD_DTC_P0562)
#define DTC_SYSTEM_VOLTAGE_HIGH ((uint16_t)OBD_DTC_P0563)
#define DTC_THROTTLE_RANGE_PERF ((uint16_t)OBD_DTC_P0121)
#define DTC_COOLANT_TEMP_RANGE ((uint16_t)OBD_DTC_P0116)
#define DTC_INTAKE_TEMP_RANGE ((uint16_t)OBD_DTC_P0111)
#define DTC_MAP_BARO_RANGE ((uint16_t)OBD_DTC_P0106)
#define DTC_FUEL_LEVEL_RANGE ((uint16_t)OBD_DTC_P0460)
#define DTC_ADJ_COMM_LOST ((uint16_t)OBD_DTC_U1908)
#define DTC_ADJ_SIGNAL_LOST ((uint16_t)OBD_DTC_U1909)
#define DTC_ADJ_FUEL_TEMP_BROKEN ((uint16_t)OBD_DTC_U190A)
#define DTC_ADJ_VOLTAGE_BAD ((uint16_t)OBD_DTC_U190B)
#define DTC_RPM_IRQ_INIT_FAIL ((uint16_t)OBD_DTC_U190C)

/** @name DTC detail (ISO 14229-1 failure type byte)
 *  Low byte of the three-byte UDS DTC. 0x88 is the ISO "bus off" type; the
 *  0xF1..0xF8 values are project specific and name the frame whose transmit
 *  failed last. */
/** @{ */
#define DTC_DETAIL_NONE UINT8_C(0x00)
#define DTC_DETAIL_CAN_BUS_OFF UINT8_C(0x88)
#define DTC_DETAIL_CAN_TX_RPM UINT8_C(0xF1)
#define DTC_DETAIL_CAN_TX_ECU_UPDATE_01 UINT8_C(0xF4)
#define DTC_DETAIL_CAN_TX_ECU_UPDATE_02 UINT8_C(0xF5)
#define DTC_DETAIL_CAN_TX_ECU_UPDATE_03 UINT8_C(0xF6)
#define DTC_DETAIL_CAN_TX_TURBO UINT8_C(0xF7)
#define DTC_DETAIL_CAN_TX_THROTTLE UINT8_C(0xF8)
#define DTC_DETAIL_CAN_TX_OTHER UINT8_C(0xF0)
/** @} */
/** @} */

#endif
