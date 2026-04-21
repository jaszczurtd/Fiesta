
#ifndef T_OBD
#define T_OBD

#include <tools_c.h>

#include "hardwareConfig.h"
#include "sensors.h"
#include "tests.h"
#include "obd-2_mapping.h"
#include "dtcManager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAD 0x00

// ── CAN addressing (11-bit standard, Ford PCM) ─────────────────────
// 7E0/7E8 = Engine ECM (PCM), 7E1/7E9 = Transmission ECM
#define REPLY_ID       0x7E8
#define LISTEN_ID      0x7E0
#define FUNCTIONAL_ID  0x7DF

// ── UDS / ISO 14229 positive-response offset ────────────────────────
#define UDS_POSITIVE_RESPONSE_OFFSET  0x40

// ── UDS negative response codes (ISO 14229 §A.1) ───────────────────
#define NRC_SERVICE_NOT_SUPPORTED      0x11
#define NRC_SUBFUNCTION_NOT_SUPPORTED  0x12
#define NRC_INCORRECT_LENGTH           0x13
#define NRC_CONDITIONS_NOT_CORRECT     0x22
#define NRC_REQUEST_OUT_OF_RANGE       0x31
#define NRC_SVC_NOT_SUPPORTED_IN_SESSION 0x7F

// ── UDS / KWP service IDs ───────────────────────────────────────────
#define UDS_SVC_DIAGNOSTIC_SESSION     0x10
#define UDS_SVC_ECU_RESET              0x11
#define UDS_SVC_READ_DATA_BY_LOCAL_ID  0x12   // KWP2000
#define UDS_SVC_CLEAR_DTC              0x14
#define KWP_SVC_READ_DTC_BY_STATUS     0x18   // KWP2000
#define UDS_SVC_READ_DTC_INFO          0x19
#define UDS_SVC_READ_DATA_BY_ID        0x22
#define UDS_SVC_READ_MEMORY_BY_ADDR    0x23   // Ford SCP DMR
#define UDS_SVC_SECURITY_ACCESS        0x27
#define UDS_SVC_COMM_CONTROL           0x28
#define UDS_SVC_WRITE_DATA_BY_ID       0x2E
#define UDS_SVC_IO_CONTROL             0x2F
#define UDS_SVC_ROUTINE_CONTROL        0x31
#define UDS_SVC_TESTER_PRESENT         0x3E
#define UDS_SVC_CONTROL_DTC_SETTING    0x85

// ── UDS positive response SIDs (service + 0x40) ────────────────────
#define UDS_RSP_DIAGNOSTIC_SESSION     0x50
#define UDS_RSP_ECU_RESET              0x51
#define UDS_RSP_READ_DATA_BY_LOCAL_ID  0x52   // KWP2000
#define UDS_RSP_CLEAR_DTC              0x54
#define KWP_RSP_READ_DTC_BY_STATUS     0x58   // KWP2000
#define UDS_RSP_READ_DTC_INFO          0x59
#define UDS_RSP_READ_DATA_BY_ID        0x62
#define UDS_RSP_READ_MEMORY_BY_ADDR    0x63   // Ford SCP DMR
#define UDS_RSP_SECURITY_ACCESS        0x67
#define UDS_RSP_COMM_CONTROL           0x68
#define UDS_RSP_WRITE_DATA_BY_ID       0x6E
#define UDS_RSP_IO_CONTROL             0x6F
#define UDS_RSP_ROUTINE_CONTROL        0x71
#define UDS_RSP_TESTER_PRESENT         0x7E
#define UDS_RSP_CONTROL_DTC_SETTING    0xC5
#define UDS_RSP_NEGATIVE               0x7F

// ── UDS session types (DiagnosticSessionControl) ────────────────────
#define UDS_SESSION_DEFAULT            0x01
#define UDS_SESSION_PROGRAMMING        0x02
#define UDS_SESSION_EXTENDED           0x03

// ── UDS suppress-positive-response bit ──────────────────────────────
#define UDS_SUPPRESS_POSITIVE_RSP      0x80

// ── OBD-II service modes (SAE J1979) ───────────────────────────────
#define OBD_MODE_CURRENT_DATA          0x01
#define OBD_MODE_FREEZE_FRAME          0x02
#define OBD_MODE_STORED_DTC            0x03
#define OBD_MODE_CLEAR_DTC             0x04
#define OBD_MODE_O2_MONITORING         0x05
#define OBD_MODE_ONBOARD_MONITORING    0x06
#define OBD_MODE_PENDING_DTC           0x07
#define OBD_MODE_CONTROL_OPERATIONS    0x08
#define OBD_MODE_VEHICLE_INFO          0x09
#define OBD_MODE_PERMANENT_DTC         0x0A

// ── OBD-II standards (PID 0x1C) ────────────────────────────────────
#define EOBD_OBD_OBD_II  9

// ── Fuel type coding (PID 0x51) ────────────────────────────────────
#define FUEL_TYPE_DIESEL  4

// ── Mode 09 PIDs ───────────────────────────────────────────────────
#define MODE09_PID_VIN       0x02
#define MODE09_PID_CALID     0x04
#define MODE09_PID_CVN       0x06
#define MODE09_PID_ECU_COUNT 0x09
#define MODE09_PID_ECU_NAME  0x0A
#define MODE09_PID_ESN       0x0D
#define MODE09_PID_TYPE_APPR 0x0F   // Exhaust regulation type approval number

// ── Ford EEC-V KWP 0x12 local identifiers ──────────────────────────
#define KWP_LID_CALIB_BLOCK       0x33
#define KWP_LID_CALIBRATION_ID    0x80
#define KWP_LID_SW_DATE           0x81
#define KWP_LID_PART_NUMBER       0x82
#define KWP_LID_MODEL_16          0x86
#define KWP_LID_VIN               0x90
#define KWP_LID_MODEL             0x91
#define KWP_LID_TYPE              0x92
#define KWP_LID_SUBTYPE           0x93
#define KWP_LID_CATCH_CODE        0x94
#define KWP_LID_VIN_ALT           0x95
#define KWP_LID_SW_VERSION        0x96
#define KWP_LID_SW_DATE_ALT       0x97
#define KWP_LID_CALIBRATION_ALT   0x98
#define KWP_LID_PART_NUMBER_ALT   0x99
#define KWP_LID_HARDWARE_ID       0x9A
#define KWP_LID_ROM_SIZE          0x9B
#define KWP_LID_COPYRIGHT         0x9C
#define KWP_LID_COMPACT_IDENT     0xFE
#define KWP_LID_SUPPORTED_LIST    0xFF

// ── Ford EEC-V UDS DIDs — standard identification ──────────────────
#define DID_ACTIVE_SESSION         0xF186
#define DID_SPARE_PART_NUMBER      0xF187
#define DID_SW_VERSION             0xF188
#define DID_SW_VERSION_ALT         0xF189
#define DID_SUPPLIER_ID            0xF18A
#define DID_MANUFACTURE_DATE       0xF18B
#define DID_SERIAL_NUMBER          0xF18C
#define DID_VIN                    0xF190
#define DID_HW_VERSION             0xF191
#define DID_SYSTEM_NAME            0xF197
#define DID_ODX_FILE_ID            0xF19E
#define DID_PART_NUMBER            0xF113
#define DID_BOOT_SW_ID             0xF180

// ── Ford EEC-V UDS DIDs — manufacturer-specific ────────────────────
#define DID_ECU_CAPABILITIES       0x0200

#define DID_FORD_MODEL             0xE6F3
#define DID_FORD_TYPE              0xE300
#define DID_FORD_VIN_CHUNK_BASE    0xE301   // E301-E305
#define DID_FORD_VIN_CHUNK_LAST    0xE305
#define DID_FORD_SW_DATE           0xE200
// E217/E21A/E219: Fordiag uses these to read Ford part number components
// for ECU identification (prefix-middle-suffix lookup in internal DB).
#define DID_FORD_PARTNUM_MIDDLE    0xE217   // binary middle  (e.g. 12A650→0x120A0650)
#define DID_FORD_PARTNUM_SUFFIX    0xE219   // encoded suffix  (2-byte Ford encoding)
#define DID_FORD_PARTNUM_PREFIX    0xE21A   // ASCII prefix    (e.g. "XS4A")
#define DID_FORD_CATCH_CODE        0xC92E
#define DID_FORD_PART_NUMBER       0xC900

// ── Ford DD0x DIDs — vehicle telemetry (CAN ECU specific) ──────────
#define DID_FORD_TOTDIST           0xDD01   // Total distance (3 bytes, km)
#define DID_FORD_FUEL_TEMP         0xDD02   // Fuel temperature (1 byte, +40 offset, °C)
#define DID_FORD_OIL_PRESSURE      0xDD03   // Oil pressure (2 bytes, kPa ×10)
#define DID_FORD_BOOST             0xDD04   // Boost pressure (2 bytes, bar ×1000)
#define DID_FORD_OUTTMP            0xDD05   // External temperature (1 byte, +40 offset)
#define DID_FORD_DPF_PRESSURE      0xDD06   // DPF differential pressure (2 bytes, Pa)
#define DID_FORD_BOOST_DESIRED     0xDD07   // Desired boost pressure (2 bytes, bar ×1000)
#define DID_FORD_BOOST_PERCENT     0xDD08   // Boost duty cycle (1 byte, 0-100 %)

// Ford F4xx identification block DIDs
#define DID_F4_MODEL               0xF400
#define DID_F4_TYPE                0xF401
#define DID_F4_SUBTYPE             0xF402
#define DID_F4_CATCH_CODE          0xF403
#define DID_F4_SW_DATE             0xF404
#define DID_F4_CALIBRATION_ID      0xF405
#define DID_F4_PART_NUMBER         0xF406
#define DID_F4_HARDWARE_ID         0xF407
#define DID_F4_ROM_SIZE            0xF408
#define DID_F4_COPYRIGHT           0xF409
#define DID_F4_MODEL_16            0xF40B
#define DID_F4_TYPE_ALT            0xF40C
#define DID_F4_SUBTYPE_ALT         0xF40D
#define DID_F4_CATCH_CODE_ALT      0xF40F
#define DID_F4_SW_DATE_ALT         0xF410
#define DID_F4_CALIBRATION_ID_ALT  0xF411
#define DID_F4_HARDWARE_ID_ALT     0xF414
#define DID_F4_ROM_SIZE_ALT        0xF442
#define DID_F4_PART_NUMBER_ALT     0xF444
#define DID_F4_SW_VERSION          0xF445
#define DID_F4_COPYRIGHT_ALT       0xF449

// ── Ford SCP PIDs (2-byte, manufacturer namespace) ─────────────────
#define SCP_PID_IDBLOCK_ADDR       0x1100
#define SCP_PID_ACT                0x1123   // Air Charge Temp
#define SCP_PID_BP                 0x1127   // Barometric Pressure
#define SCP_PID_ECT                0x1139   // Engine Coolant Temp
#define SCP_PID_TP_ENG             0x1154   // Throttle Position A/D
#define SCP_PID_KAMRF1             0x1156   // KAM fuel ratio bank 1
#define SCP_PID_KAMRF2             0x1157   // KAM fuel ratio bank 2
#define SCP_PID_LAMBSE1            0x1158   // Lambda sensor 1
#define SCP_PID_LAMBSE2            0x1159   // Lambda sensor 2
#define SCP_PID_LOAD               0x115A   // Engine Load
#define SCP_PID_RPM                0x1165   // Engine RPM
#define SCP_PID_RATCH              0x1169   // Throttle ratchet
#define SCP_PID_VBAT               0x1172   // Battery Voltage
#define SCP_PID_NORPM              0x11B5   // Neutral output RPM
#define SCP_PID_VS                 0x11C1   // Vehicle Speed
#define SCP_PID_IMAF               0x1633   // MAF sensor A/D
#define SCP_PID_SECURITY_STATUS    0xC115   // Security Access Status
#define SCP_PID_PATS_STATUS        0xC124   // PATS Status

// ── Ford SCP PIDs — diesel-specific (CDAN2 PID MAP) ────────────────
#define SCP_PID_TRIP_COUNT         0x0100   // OBDII trip count, Byte
#define SCP_PID_CODES_COUNT        0x0200   // DTC code count, Byte
#define SCP_PID_EGRDC              0x113C   // EGR duty cycle, 100/32768 %
#define SCP_PID_FUELPW1            0x1141   // Fuel pulsewidth 1, 32 ticks
#define SCP_PID_VMAF               0x1177   // MAF voltage, 0.000244V (CRAI8)
#define SCP_PID_MAF_RATE           0x1671   // j1979_01_10, 0.01 g/s

// ── Ford SCP DMR (Direct Memory Request) constants ─────────────────
#define SCP_IDBLOCK_BANK           0x09
#define SCP_IDBLOCK_ADDR           0xFF00
#define SCP_IDBLOCK_ADDR_ALT       0x9F00
#define SCP_IDBLOCK_FMT_DEFAULT    0xFF
#define SCP_IDBLOCK_SIZE           256
#define SCP_IDBLOCK_VIN_OFFSET     0x85   // VIN start (17 bytes, ends at 0x95)
#define SCP_IDBLOCK_COPYRIGHT_OFS  0x97   // Copyright start (32 bytes)
#define SCP_IDBLOCK_CHKSUM_OFS     0xFE   // Checksum correction word

// ── Ford EEC-V ROM size (512 KB) ───────────────────────────────────
#define FORD_ROM_SIZE_512K         0x00080000u

// ── Ford EEC-V identification field padding ────────────────────────
#define FORD_IDENT_PAD             0x20   // space padding for ident fields

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

/**
 * @brief Initialize the OBD/UDS CAN responder.
 * @param retries Number of CAN initialization retries to attempt.
 * @return None.
 */
void obdInit(int retries);

/**
 * @brief Handle one raw OBD/UDS request frame payload.
 * @param requestId Incoming CAN request identifier.
 * @param data Pointer to request bytes (expects at least CAN DLC bytes).
 * @return None.
 */
void obdReq(uint32_t requestId, uint8_t *data);

/**
 * @brief Poll CAN and advance the OBD/ISO-TP state machine.
 * @return None.
 */
void obdLoop(void);

#ifdef OBD_ENABLE_TOTDIST
/**
 * @brief Read the emulated total-distance value exposed through Ford DIDs.
 * @return Current odometer value in kilometers.
 */
uint32_t obdGetTotalDistanceKm(void);

/**
 * @brief Update the emulated total-distance value exposed through Ford DIDs.
 * @param km New odometer value in kilometers.
 * @return None.
 */
void     obdSetTotalDistanceKm(uint32_t km);
#endif

/**
 * @brief Encode only the payload bytes for a supported Mode 01 PID.
 * @param pid PID to encode.
 * @param out Output buffer receiving only the data bytes.
 * @param outLen Output pointer receiving number of bytes written.
 * @return True when the PID is supported, otherwise false.
 */
bool encodeMode01PidData(uint8_t pid, uint8_t *out, int *outLen);

/**
 * @brief Build a compact DTC payload for Mode 03/07/0A style responses.
 * @param responseService Positive-response service identifier.
 * @param kind DTC set to export.
 * @param outData Output buffer receiving the packed payload.
 * @param maxLen Size of @p outData in bytes.
 * @return Number of bytes written to @p outData.
 */
int fillDtcPayload(uint8_t responseService, dtc_kind_t kind, uint8_t *outData, int maxLen);

/**
 * @brief Split a Ford part number string into prefix, middle and suffix spans.
 * @param pn Null-terminated Ford part number string.
 * @param prefixOut Output pointer receiving the prefix start.
 * @param prefixLen Output pointer receiving the prefix length.
 * @param middleOut Output pointer receiving the middle section start.
 * @param middleLen Output pointer receiving the middle section length.
 * @param suffixOut Output pointer receiving the suffix start.
 * @param suffixLen Output pointer receiving the suffix length.
 * @return True when the input matches PREFIX-MIDDLE-SUFFIX format.
 */
bool fordPartNumberSplit(const char *pn,
                         const char **prefixOut, int *prefixLen,
                         const char **middleOut, int *middleLen,
                         const char **suffixOut, int *suffixLen);

/**
 * @brief Encode one Ford part-number suffix fragment into Fordiag byte form.
 * @param s Pointer to the suffix characters to encode.
 * @param len Number of characters to encode from @p s.
 * @return Encoded Ford suffix byte.
 */
uint8_t fordPartSuffixCharsToByte(const char *s, int len);

#ifdef __cplusplus
}
#endif

#endif
