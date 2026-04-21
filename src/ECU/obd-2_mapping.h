#ifndef T_OBD2_MAPPING
#define T_OBD2_MAPPING

#include <tools_c.h>
#include "obd-2_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

// DTC codes mapped to OBD-2/U-code format for better tester compatibility.
// Encoding follows SAE J2012 2-byte layout: system + code digits.
#define DTC_OBD_CAN_INIT_FAIL      0xD900  // U1900 Network/CAN communication fault
#define DTC_PCF8574_COMM_FAIL      0xC073  // U0073 Control Module Communication Bus Off
#define DTC_PWM_CHANNEL_NOT_INIT   0x0657  // P0657 Actuator Supply Voltage "A" Circuit/Open
#define DTC_DPF_COMM_LOST          0xC100  // U0100 Lost communication with DPF module (project mapping)
#define DTC_EGT_COMM_LOST          0xD902  // U1902 Lost communication with EGT module

// Proposed future DTC set (project catalog).
// Note: A code becomes reportable only after adding it to dtcManager.c state table.
#define DTC_CAN0_INIT_FAIL         0xD903  // U1903 CAN0 bus init failure
#define DTC_GPS_SIGNAL_LOST        0xD904  // U1904 GPS data unavailable/stale
#define DTC_SD_LOGGER_NOT_READY    0xD905  // U1905 SD logger missing/not initialized
#define DTC_ISOTP_FC_TIMEOUT       0xD906  // U1906 ISO-TP flow-control timeout
#define DTC_ISOTP_FC_ABORT         0xD907  // U1907 ISO-TP flow-control abort from tester

#define DTC_ENGINE_OVERSPEED       0x0219  // P0219 Engine overspeed condition
#define DTC_ECM_EEPROM_FAULT       0x062F  // P062F Internal control module EEPROM error
#define DTC_SYSTEM_VOLTAGE_LOW     0x0562  // P0562 System voltage low
#define DTC_SYSTEM_VOLTAGE_HIGH    0x0563  // P0563 System voltage high
#define DTC_THROTTLE_RANGE_PERF    0x0121  // P0121 Throttle/Pedal position range/performance
#define DTC_COOLANT_TEMP_RANGE     0x0116  // P0116 Engine coolant temperature range/performance
#define DTC_INTAKE_TEMP_RANGE      0x0111  // P0111 Intake air temperature range/performance
#define DTC_MAP_BARO_RANGE         0x0106  // P0106 MAP/BARO pressure range/performance
#define DTC_FUEL_LEVEL_RANGE       0x0460  // P0460 Fuel level sensor range/performance

// Adjustometer (VP37 feedback module) DTCs — active in dtcManager.c state table.
#define DTC_ADJ_COMM_LOST          0xD908  // U1908 Lost communication with Adjustometer module
#define DTC_ADJ_SIGNAL_LOST        0xD909  // U1909 Adjustometer oscillator signal lost
#define DTC_ADJ_FUEL_TEMP_BROKEN   0xD90A  // U190A Adjustometer fuel temperature sensor fault
#define DTC_ADJ_VOLTAGE_BAD        0xD90B  // U190B Adjustometer supply voltage out of range

/**
 * @brief Translate a PID number into a human-readable label.
 * @param pid PID number to describe.
 * @return Pointer to a static name string.
 */
const char *getPIDName(int pid);

/**
 * @brief Translate a project DTC code into a human-readable label.
 * @param code Diagnostic trouble code to describe.
 * @return Pointer to a static name string.
 */
const char *getDtcName(uint16_t code);

#ifdef __cplusplus
}
#endif

#endif
