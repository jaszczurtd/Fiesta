#ifndef T_OBD2_MAPPING
#define T_OBD2_MAPPING

#include <tools.h>
#include "obd-2_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

// DTC codes mapped to OBD-2/U-code format for better tester compatibility.
// Encoding follows SAE J2012 2-byte layout: system + code digits.
#define DTC_OBD_CAN_INIT_FAIL      0xD900  // U1900 Network/CAN communication fault
#define DTC_PCF8574_COMM_FAIL      0xC073  // U0073 Control Module Communication Bus Off
#define DTC_PWM_CHANNEL_NOT_INIT   0x0657  // P0657 Actuator Supply Voltage "A" Circuit/Open
#define DTC_DPF_COMM_LOST          0xC100  // U0100 Lost Communication With ECM/PCM "A"

const char *getPIDName(int pid);
const char *getDtcName(uint16_t code);

#ifdef __cplusplus
}
#endif

#endif
