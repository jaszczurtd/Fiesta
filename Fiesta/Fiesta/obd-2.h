
#ifndef T_OBD
#define T_OBD

#include <mcp_can.h>
#include <SPI.h>
#include <tools.h>

#include "start.h"
#include "hardwareConfig.h"
#include "sensors.h"
#include "tests.h"

//Default reply ECU ID
#define REPLY_ID 0x7E8 

//OBD standards https://en.wikipedia.org/wiki/OBD-II_PIDs#Service_01_PID_1C
#define JOBD_OBD_II 11
#define EOBD_OBD_OBD_II 9

//Fuel Type Coding https://en.wikipedia.org/wiki/OBD-II_PIDs#Fuel_Type_Coding
#define FUEL_TYPE_DIESEL 4

enum {
  L1 = 0x01,
  L2 = 0x02,
  L3 = 0x03,
  L4 = 0x04,
  L5 = 0x05,
  L6 = 0x06,
  L7 = 0x07,
  L8 = 0x08,
  L9 = 0x09,
  L10 = 0x0a,
  L11 = 0x0b,
  L12 = 0x0c,
  L13 = 0x0d,
  L14 = 0x0e,
  L15 = 0x0f,
};

#define MODE1_RESPONSE    0x41
#define MODE3_RESPONSE    0x43
#define MODE4_RESPONSE    0x44

#define SHOW_CURRENT_DATA 1
#define SHOW_STORED_DIAGNOSTIC_TROUBLE_CODES 3
#define CLEAR_DIAGNOSTIC_TROUBLE_CODES_AND_STORED_VALUES 4
#define REQUEST_VEHICLE_INFORMATION 9

#define REQUEST_MODE_9_SUPPORTED 0x00
#define REQUEST_VIN 0x02
#define REQUEST_CALLIBRATION_ID 0x04
#define REQUEST_ECU_NAME 0x0a

enum {
  PIDS_SUPPORT_01_20                                = 0x00,
  MONITOR_STATUS_SINCE_DTCS_CLEARED                 = 0x01,
  FREEZE_DTC                                        = 0x02,
  FUEL_SYSTEM_STATUS                                = 0x03,
  CALCULATED_ENGINE_LOAD                            = 0x04,
  ENGINE_COOLANT_TEMPERATURE                        = 0x05,
  SHORT_TERM_FUEL_TRIM_BANK_1                       = 0x06,
  LONG_TERM_FUEL_TRIM_BANK_1                        = 0x07,
  SHORT_TERM_FUEL_TRIM_BANK_2                       = 0x08,
  LONG_TERM_FUEL_TRIM_BANK_2                        = 0x09,
  FUEL_PRESSURE                                     = 0x0a,
  INTAKE_MANIFOLD_ABSOLUTE_PRESSURE                 = 0x0b,
  ENGINE_RPM                                        = 0x0c,
  VEHICLE_SPEED                                     = 0x0d,
  TIMING_ADVANCE                                    = 0x0e,
  AIR_INTAKE_TEMPERATURE                            = 0x0f,
  MAF_AIR_FLOW_RATE                                 = 0x10,
  THROTTLE_POSITION                                 = 0x11,
  COMMANDED_SECONDARY_AIR_STATUS                    = 0x12,
  OXYGEN_SENSORS_PRESENT_IN_2_BANKS                 = 0x13,
  OXYGEN_SENSOR_1_SHORT_TERM_FUEL_TRIM              = 0x14,
  OXYGEN_SENSOR_2_SHORT_TERM_FUEL_TRIM              = 0x15,
  OXYGEN_SENSOR_3_SHORT_TERM_FUEL_TRIM              = 0x16,
  OXYGEN_SENSOR_4_SHORT_TERM_FUEL_TRIM              = 0x17,
  OXYGEN_SENSOR_5_SHORT_TERM_FUEL_TRIM              = 0x18,
  OXYGEN_SENSOR_6_SHORT_TERM_FUEL_TRIM              = 0x19,
  OXYGEN_SENSOR_7_SHORT_TERM_FUEL_TRIM              = 0x1a,
  OXYGEN_SENSOR_8_SHORT_TERM_FUEL_TRIM              = 0x1b,
  OBD_STANDARDS_THIS_VEHICLE_CONFORMS_TO            = 0x1c,
  OXYGEN_SENSORS_PRESENT_IN_4_BANKS                 = 0x1d,
  AUXILIARY_INPUT_STATUS                            = 0x1e,
  RUN_TIME_SINCE_ENGINE_START                       = 0x1f,

  PIDS_SUPPORT_21_40                                = 0x20,
  DISTANCE_TRAVELED_WITH_MIL_ON                     = 0x21,
  FUEL_RAIL_PRESSURE                                = 0x22,
  FUEL_RAIL_GAUGE_PRESSURE                          = 0x23,
  OXYGEN_SENSOR_1_FUEL_AIR_EQUIVALENCE_RATIO        = 0x24,
  OXYGEN_SENSOR_2_FUEL_AIR_EQUIVALENCE_RATIO        = 0x25,
  OXYGEN_SENSOR_3_FUEL_AIR_EQUIVALENCE_RATIO        = 0x26,
  OXYGEN_SENSOR_4_FUEL_AIR_EQUIVALENCE_RATIO        = 0x27,
  OXYGEN_SENSOR_5_FUEL_AIR_EQUIVALENCE_RATIO        = 0x28,
  OXYGEN_SENSOR_6_FUEL_AIR_EQUIVALENCE_RATIO        = 0x29,
  OXYGEN_SENSOR_7_FUEL_AIR_EQUIVALENCE_RATIO        = 0x2a,
  OXYGEN_SENSOR_8_FUEL_AIR_EQUIVALENCE_RATIO        = 0x2b,
  COMMANDED_EGR                                     = 0x2c,
  EGR_ERROR                                         = 0x2d,
  COMMANDED_EVAPORATIVE_PURGE                       = 0x2e,
  FUEL_TANK_LEVEL_INPUT                             = 0x2f,
  WARM_UPS_SINCE_CODES_CLEARED                      = 0x30,
  DISTANCE_TRAVELED_SINCE_CODES_CLEARED             = 0x31,
  EVAP_SYSTEM_VAPOR_PRESSURE                        = 0x32,
  ABSOLULTE_BAROMETRIC_PRESSURE                     = 0x33,
/*OXYGEN_SENSOR_1_FUEL_AIR_EQUIVALENCE_RATIO        = 0x34,
  OXYGEN_SENSOR_2_FUEL_AIR_EQUIVALENCE_RATIO        = 0x35,
  OXYGEN_SENSOR_3_FUEL_AIR_EQUIVALENCE_RATIO        = 0x36,
  OXYGEN_SENSOR_4_FUEL_AIR_EQUIVALENCE_RATIO        = 0x37,
  OXYGEN_SENSOR_5_FUEL_AIR_EQUIVALENCE_RATIO        = 0x38,
  OXYGEN_SENSOR_6_FUEL_AIR_EQUIVALENCE_RATIO        = 0x39,
  OXYGEN_SENSOR_7_FUEL_AIR_EQUIVALENCE_RATIO        = 0x3a,
  OXYGEN_SENSOR_8_FUEL_AIR_EQUIVALENCE_RATIO        = 0x3b,*/
  CATALYST_TEMPERATURE_BANK_1_SENSOR_1              = 0x3c,
  CATALYST_TEMPERATURE_BANK_2_SENSOR_1              = 0x3d,
  CATALYST_TEMPERATURE_BANK_1_SENSOR_2              = 0x3e,
  CATALYST_TEMPERATURE_BANK_2_SENSOR_2              = 0x3f,

  PIDS_SUPPORT_41_60                                = 0x40,
  MONITOR_STATUS_THIS_DRIVE_CYCLE                   = 0x41,
  CONTROL_MODULE_VOLTAGE                            = 0x42,
  ABSOLUTE_LOAD_VALUE                               = 0x43,
  FUEL_AIR_COMMANDED_EQUIVALENCE_RATE               = 0x44,
  RELATIVE_THROTTLE_POSITION                        = 0x45,
  AMBIENT_AIR_TEMPERATURE                           = 0x46,
  ABSOLUTE_THROTTLE_POSITION_B                      = 0x47,
  ABSOLUTE_THROTTLE_POSITION_C                      = 0x48,
  ABSOLUTE_THROTTLE_POSITION_D                      = 0x49,
  ABSOLUTE_THROTTLE_POSITION_E                      = 0x4a,
  ABSOLUTE_THROTTLE_POSITION_F                      = 0x4b,
  COMMANDED_THROTTLE_ACTUATOR                       = 0x4c,
  TIME_RUN_WITH_MIL_ON                              = 0x4d,
  TIME_SINCE_TROUBLE_CODES_CLEARED                  = 0x4e,
/*                                                  = 0x4f,
                                                    = 0x50,*/
  FUEL_TYPE                                         = 0x51,
  ETHANOL_FUEL_PERCENTAGE                           = 0x52,
  ABSOLUTE_EVAP_SYSTEM_VAPOR_PRESSURE               = 0x53,
/*EVAP_SYSTEM_VAPOR_PRESSURE                        = 0x54,*/
/*                                                  = 0x55,
                                                    = 0x56,
                                                    = 0x57,
                                                    = 0x58,*/
  FUEL_RAIL_ABSOLUTE_PRESSURE                       = 0x59,
  RELATIVE_ACCELERATOR_PEDAL_POSITTION              = 0x5a,
  HYBRID_BATTERY_PACK_REMAINING_LIFE                = 0x5b,
  ENGINE_OIL_TEMPERATURE                            = 0x5c,
  FUEL_INJECTION_TIMING                             = 0x5d,
  ENGINE_FUEL_RATE                                  = 0x5e,
  EMISSION_REQUIREMENT_TO_WHICH_VEHICLE_IS_DESIGNED = 0x5f,

  // more PIDs can be added from: https://en.wikipedia.org/wiki/OBD-II_PIDs
};

#define PID_LAST 0x5f

void obdInit(int retries);
void obdLoop(void);
const char *getPIDName(int pid);

//=================================================================
//Define ECU Supported PID's
//=================================================================

// Define the set of PIDs for MODE01 you wish you ECU to support.  For more information, see:
// https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00
//
// PID 0x01 (1) - Monitor status since DTCs cleared. (Includes malfunction indicator lamp (MIL) status and number of DTCs.)
// |   PID 0x05 (05) - Engine Coolant Temperature
// |   |      PID 0x0C (12) - Engine RPM
// |   |      |PID 0x0D (13) - Vehicle speed
// |   |      ||PID 0x0E (14) - Timing advance
// |   |      |||PID 0x0F (15) - Intake air temperature
// |   |      ||||PID 0x10 (16) - MAF Air Flow Rate
// |   |      |||||            PID 0x1C (28) - OBD standards this vehicle conforms to
// |   |      |||||            |                              PID 0x51 (58) - Fuel Type
// |   |      |||||            |                              |
// v   V      VVVVV            V                              v
// 10001000000111110000:000000010000000000000:0000000000000000100
// Converted to hex, that is the following four byte value binary to hex
// 0x881F0000 0x00 PID 01 -20
// 0x02000000 0x20 PID 21 - 40
// 0x04000000 0x40 PID 41 - 60

// Next, we'll create the bytearray that will be the Supported PID query response data payload using the four bye supported pi hex value
// we determined above (0x081F0000):

//                               0x06 - additional meaningful bytes after this one (1 byte Service Mode, 1 byte PID we are sending, and the four by Supported PID value)
//                                |    0x41 - This is a response (0x40) to a service mode 1 (0x01) query.  0x40 + 0x01 = 0x41
//                                |     |    0x00 - The response is for PID 0x00 (Supported PIDS 1-20)
//                                |     |     |    0x88 - The first of four bytes of the Supported PIDS value
//                                |     |     |     |    0x1F - The second of four bytes of the Supported PIDS value
//                                |     |     |     |     |    0x00 - The third of four bytes of the Supported PIDS value
//                                |     |     |     |     |      |   0x00 - The fourth of four bytes of the Supported PIDS value
//                                |     |     |     |     |      |    |    0x00 - OPTIONAL - Just extra zeros to fill up the 8 byte CAN message data payload)
//                                |     |     |     |     |      |    |     |
//                                V     V     V     V     V      V    V     V
//byte md1Supported0x00PID[8] = {0x06, 0x41, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff};
//byte md1Supported0x20PID[8] = {0x06, 0x41, 0x20, 0xff, 0xff, 0xff, 0xff, 0xff};
//byte md1Supported0x40PID[8] = {0x06, 0x41, 0x40, 0xff, 0xff, 0xff, 0xff, 0xff};

// Define the set of PIDs for MODE09 you wish you ECU to support.
// As per the information on bitwise encoded PIDs (https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00)
// Our supported PID value is:
//
//  PID 0x02 - Vehicle Identification Number (VIN)
//  | PID 0x04 (04) - Calibration ID
//  | |     PID 0x0C (12) - ECU NAME
//  | |     |
//  V V     V
// 01010000010  // Converted to hex, that is the following four byte value binary to hex
// 0x28200000 0x00 PID 01-11

// Next, we'll create the bytearray that will be the Supported PID query response data payload using the four bye supported pi hex value
// we determined above (0x28200000):

//                               0x06 - additional meaningful bytes after this one (1 byte Service Mode, 1 byte PID we are sending, and the four by Supported PID value)
//                                |    0x41 - This is a response (0x40) to a service mode 1 (0x01) query.  0x40 + 0x01 = 0x41
//                                |     |    0x00 - The response is for PID 0x00 (Supported PIDS 1-20)
//                                |     |     |    0x28 - The first of four bytes of the Supported PIDS value
//                                |     |     |     |    0x20 - The second of four bytes of the Supported PIDS value
//                                |     |     |     |     |    0x00 - The third of four bytes of the Supported PIDS value
//                                |     |     |     |     |      |   0x00 - The fourth of four bytes of the Supported PIDS value
//                                |     |     |     |     |      |    |    0x00 - OPTIONAL - Just extra zeros to fill up the 8 byte CAN message data payload)
//                                |     |     |     |     |      |    |     |
//                                V     V     V     V     V      V    V     V
//byte md9Supported0x00PID[8] = {0x06, 0x49, 0x00, 0x28, 0x28, 0x00, 0x00, 0x00};

#endif
