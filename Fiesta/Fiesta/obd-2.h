
#ifndef T_OBD
#define T_OBD

#include <mcp_can.h>
#include <SPI.h>
#include <tools.h>

#include "start.h"
#include "hardwareConfig.h"
#include "sensors.h"
#include "tests.h"

#define PAD 0x00

// What CAN ID type?  Standard or Extended
#define standard 1

// 7E0/8 = Engine ECM
// 7E1/9 = Transmission ECM

#if standard == 1
  #define REPLY_ID 0x7E9
  #define LISTEN_ID 0x7E1
  #define FUNCTIONAL_ID 0x7DF  
#else
  #define REPLY_ID 0x98DAF101
  #define LISTEN_ID 0x98DA01F1
  #define FUNCTIONAL_ID 0x98DB33F1
#endif


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

/* Details from http://en.wikipedia.org/wiki/OBD-II_PIDs */
#define PID_0_20            0x00    //PID 0 - 20 supported
#define STATUS_DTC          0x01    ///
#define FREEZE_DTC          0x02    ///
#define FUEL_SYS_STATUS     0x03    ///
#define ENGINE_LOAD         0x04    //
#define ENGINE_COOLANT_TEMP 0x05
#define ST_FUEL_TRIM_1      0x06    ///
#define LT_FUEL_TRIM_1      0x07    ///
#define ST_FUEL_TRIM_2      0x08    ///
#define LT_FUEL_TRIM_2      0x09    ///
#define FUEL_PRESSURE       0x0A    //
#define INTAKE_PRESSURE     0x0B    //
#define ENGINE_RPM          0x0C
#define VEHICLE_SPEED       0x0D
#define TIMING_ADVANCE      0x0E    //
#define INTAKE_TEMP         0x0F    //
#define MAF_SENSOR          0x10
#define THROTTLE            0x11
#define COMMANDED_SEC_AIR   0x12    ///
#define O2_SENS_PRES        0x13    ///
#define O2_B1S1_VOLTAGE     0x14    ///
#define O2_B1S2_VOLTAGE     0x15    ///
#define O2_B1S3_VOLTAGE     0x16    ///
#define O2_B1S4_VOLTAGE     0x17    ///
#define O2_B2S1_VOLTAGE     0x18    ///
#define O2_B2S2_VOLTAGE     0x19    ///
#define O2_B2S3_VOLTAGE     0x1A    ///
#define O2_B2S4_VOLTAGE     0x1B    ///
#define OBDII_STANDARDS     0x1C    //List of OBDII Standars the car conforms to
#define O2_SENS_PRES_ALT    0x1D    ///
#define AUX_IN_STATUS       0x1E    ///
#define ENGINE_RUNTIME      0x1F    //
#define PID_21_40           0x20    //PID 21-40 supported
#define DIST_TRAVELED_MIL   0x21    ///
#define FUEL_RAIL_PRESSURE  0x22    //
#define FUEL_RAIL_PRES_ALT  0x23    ///
#define O2S1_WR_LAMBDA_V    0x24    ///
#define O2S2_WR_LAMBDA_V    0x25    ///
#define O2S3_WR_LAMBDA_V    0x26    ///
#define O2S4_WR_LAMBDA_V    0x27    ///
#define O2S5_WR_LAMBDA_V    0x28    ///
#define O2S6_WR_LAMBDA_V    0x29    ///
#define O2S7_WR_LAMBDA_V    0x2A    ///
#define O2S8_WR_LAMBDA_V    0x2B    ///
#define COMMANDED_EGR       0x2C    //
#define EGR_ERROR           0x2D    //
#define COMMANDED_EVAP_P    0x2E    ///
#define FUEL_LEVEL          0x2F    //
#define WARMUPS_SINCE_CLR   0x30    ///
#define DIST_SINCE_CLR      0x31    ///
#define EVAP_PRESSURE       0x32    //
#define BAROMETRIC_PRESSURE 0x33    //
#define O2S1_WR_LAMBDA_I    0x34    ///
#define O2S2_WR_LAMBDA_I    0x35    ///
#define O2S3_WR_LAMBDA_I    0x36    ///
#define O2S4_WR_LAMBDA_I    0x37    ///
#define O2S5_WR_LAMBDA_I    0x38    ///
#define O2S6_WR_LAMBDA_I    0x39    ///
#define O2S7_WR_LAMBDA_I    0x3A    ///
#define O2S8_WR_LAMBDA_I    0x3B    ///
#define CAT_TEMP_B1S1       0x3C    ///
#define CAT_TEMP_B1S2       0x3E    ///
#define CAT_TEMP_B2S1       0x3D    ///
#define CAT_TEMP_B2S2       0x3F    ///
#define PID_41_60           0x40    //PID 41-60 supported
#define MONITOR_STATUS      0x41    ///
#define ECU_VOLTAGE         0x42    //
#define ABSOLUTE_LOAD       0x43    //
#define COMMANDED_EQUIV_R   0x44    ///
#define REL_THROTTLE_POS    0x45    ///
#define AMB_AIR_TEMP        0x46    ///
#define ABS_THROTTLE_POS_B  0x47    ///
#define ABS_THROTTLE_POS_C  0x48    ///
#define ACCEL_POS_D         0x49    ///
#define ACCEL_POS_E         0x4A    ///
#define ACCEL_POS_F         0x4B    ///
#define COMMANDED_THROTTLE  0x4C    ///
#define TIME_RUN_WITH_MIL   0x4D    ///
#define TIME_SINCE_CLR      0x4E    ///
#define MAX_R_O2_VI_PRES    0x4F    ///
#define MAX_AIRFLOW_MAF     0x50    ///
#define FUEL_TYPE           0x51    //
#define ETHANOL_PERCENT     0x52    //
#define ABS_EVAP_SYS_PRES   0x53    ///
#define EVAP_SYS_PRES       0x54    ///
#define ST_O2_TRIM_B1B3     0x55    ///
#define LT_O2_TRIM_B1B3     0x56    ///
#define ST_02_TRIM_B2B4     0x57    ///
#define LT_O2_TRIM_B2B4     0x58    ///
#define ABS_FUEL_RAIL_PRES  0x59    ///
#define REL_ACCEL_POS       0x5A    ///
#define HYBRID_BATT_PCT     0x5B    ///
#define ENGINE_OIL_TEMP     0x5C    ///
#define FUEL_TIMING         0x5D    //
#define FUEL_RATE           0x5E    //
#define EMISSIONS_STANDARD  0x5F    ///
#define PID_61_80           0x60
#define DEMANDED_TORQUE     0x61    ///
#define ACTUAL_TORQUE       0x62    ///
#define REFERENCE_TORQUE    0x63    //
#define ENGINE_PCT_TORQUE   0x64    ///
#define AUX_IO_SUPPORTED    0x65    ///
#define P_MAF_SENSOR        0x66    ///
#define P_ENGINE_COOLANT_T  0x67    ///
#define P_INTAKE_TEMP       0x68    ///
#define P_COMMANDED_EGR     0x69    ///
#define P_COMMANDED_INTAKE  0x6A    ///
#define P_EGR_TEMP          0x6B    ///
#define P_COMMANDED_THROT   0x6C    ///
#define P_FUEL_PRESSURE     0x6D    ///
#define P_FUEL_INJ_PRES     0x6E    ///
#define P_TURBO_PRESSURE    0x6F    ///
#define P_BOOST_PRES_CONT   0x70    ///
#define P_VGT_CONTROL       0x71    ///
#define P_WASTEGATE_CONT    0x72    ///
#define P_EXHAUST_PRESSURE  0x73    ///
#define P_TURBO_RPM         0x74    ///
#define P_TURBO_TEMP1       0x75    ///
#define P_TURBO_TEMP2       0x76    ///
#define P_CACT              0x77    ///
#define P_EGT_B1            0x78    ///
#define P_EGT_B2            0x79    ///
#define P_DPF1              0x7A    ///
#define P_DPF2              0x7B    ///
#define P_DPF_TEMP          0x7C    ///
#define P_NOX_NTE_STATUS    0x7D    ///
#define P_PM_NTE_STATUS     0x7E    ///
#define P_ENGINE_RUNTUME    0x7F    ///
#define PID_81_A0           0x80
#define P_ENGINE_AECD_1     0x81    ///
#define P_ENGINE_AECD_2     0x82    ///
#define P_NOX_SENSOR        0x83    ///
#define P_MANIFOLD_TEMP     0x84    ///
#define P_NOX_SYSTEM        0x85    ///
#define P_PM_SENSOR         0x86    ///
#define P_IN_MANIF_TEMP     0x87    ///
#define PID_A1_C0           0xA0     
#define PID_C1_E0           0xC0
#define PID_E1_FF           0xE0
#define PID_LAST 0x5f

#define EURO_0 0x00 
#define EURO_1 0x01
#define EURO_2 0x02
#define EURO_3 0x03
#define EURO_4 0x04
#define EURO_5 0x05
#define EURO_6 0x06

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
