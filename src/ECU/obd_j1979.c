/* SAE J1979 services and PID encoders. */

#include "obd_internal.h"
#include "obd_protocol.h"

#include <JaszczurHAL.h>
#include <hal/core/jh_endian.h>

#include "config.h"
#include "dtcManager.h"
#include "ecu_unit_testing.h"
#include "hardwareConfig.h"
#include "rpm.h"
#include "sensors.h"
#include "tests.h"
#include "vp37.h"

#ifdef UNIT_TEST
#include "tests/testable/obd2_testable.h"
#endif

TESTABLE_STATIC int fillDtcPayload(uint8_t responseService, dtc_kind_t kind,
                                   uint8_t *outData, int maxLen) {
  if (outData == NULL || maxLen < 2) {
    return 0;
  }

  uint16_t codes[8];
  (void)memset(codes, 0, sizeof(codes));
  uint8_t count = dtcManagerGetCodes(kind, codes, (uint8_t)COUNTOF(codes));

  outData[0] = responseService;
  outData[1] = count;

  int pos = 2;
  for (uint8_t i = 0u; (i < count) && ((pos + 1) < maxLen); i++) {
    jh_store_be16(&outData[pos], codes[i]);
    pos += 2;
  }

  return pos;
}
typedef void (*mode01_encoder_t)(uint8_t *txData);

typedef struct {
  uint8_t pid;
  mode01_encoder_t encoder;
} mode01_pid_handler_t;

static void unsupportedPrint(uint8_t mode, uint8_t pid) {
  deb("Mode $%02X: Unsupported PID $%02X requested!", mode, pid);
}

/**
 * @brief Encode the Mode 01 supported-PID bitmap for 0x00-0x20.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01Pid_00(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x98; // PIDs 01,04,05 (removed 03=FuelSysStatus for diesel)
  txData[4] = 0x3A; // PIDs 0B,0C,0D,0F
  txData[5] = 0x80;
  txData[6] = 0x13;
}

/**
 * @brief Encode MIL and active-DTC count for Mode 01 PID 0x01.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01StatusDtc(uint8_t *txData) {
  uint8_t activeDTC = dtcManagerCount(DTC_KIND_ACTIVE);
  bool MIL = (activeDTC > 0u);
  txData[0] = 0x06;
  txData[3] = (MIL << 7) | (activeDTC & 0x7F);
  txData[4] = 0x07;
  txData[5] = 0xFF;
  txData[6] = 0x00;
}

/**
 * @brief Convert a temperature in °C into the 1-byte OBD/UDS representation.
 * @param tempC Temperature value in degrees Celsius.
 * @return Raw byte in the range 0..255 using the +40 °C OBD offset.
 * @note OBD-II (SAE J1979) encodes temperatures as A = (T + 40) with a 1-byte
 *       range of -40 °C .. 215 °C. Clamping prevents undefined-cast values when
 *       upstream readings momentarily fall outside the sensor spec.
 */
uint8_t obd_encodeTempByte(float tempC) {
  int32_t raw = (int32_t)(tempC + 40.0f);
  if (raw < 0) {
    raw = 0;
  } else if (raw > 255) {
    raw = 255;
  }
  return (uint8_t)raw;
}

/**
 * @brief Encode diesel fuel-system status for Mode 01 PID 0x03.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01FuelSysStatus(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = 0;
  txData[4] = 0;
}

/**
 * @brief Encode calculated engine load for Mode 01 PID 0x04.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01EngineLoad(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] =
      hal_math_percent_to_value(getGlobalValue(F_CALCULATED_ENGINE_LOAD), 255);
}

/**
 * @brief Encode absolute engine load for Mode 01 PID 0x43.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01AbsoluteLoad(uint8_t *txData) {
  txData[0] = 0x04;
  int l =
      hal_math_percent_to_value(getGlobalValue(F_CALCULATED_ENGINE_LOAD), 255);
  jh_store_be16(&txData[3], l);
}

/**
 * @brief Encode coolant temperature for Mode 01 PID 0x05.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01CoolantTemp(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = obd_encodeTempByte(getGlobalValue(F_COOLANT_TEMP));
}

/**
 * @brief Encode absolute intake pressure for Mode 01 PID 0x0B.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01IntakePressure(uint8_t *txData) {
  // PID 0x0B: 1 byte, kPa absolute (0-255)
  // F_PRESSURE is gauge bar (above atmosphere); convert: kPa_abs = bar*100 +
  // 101
  int32_t kpa = (int32_t)(getGlobalValue(F_PRESSURE) * 100.0f) + 101;
  if (kpa < 0) {
    kpa = 0;
  }
  if (kpa > 255) {
    kpa = 255;
  }
  txData[0] = 0x03;
  txData[3] = (uint8_t)kpa;
}

/**
 * @brief Encode fuel pressure placeholder for Mode 01 PID 0x0A.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01FuelPressure(uint8_t *txData) {
  // PID 0x0A: gauge fuel pressure, 1 byte, kPa = 3*A. Not applicable for diesel
  // VP37.
  txData[0] = 0x03;
  txData[3] = 0;
}

/**
 * @brief Encode VP37 rail-pressure proxy for diesel-specific fuel rail PIDs.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01FuelRailPressureAlt(uint8_t *txData) {
  txData[0] = 0x04;
  const RPM *rpm = getRPMInstance();
  int p = RPM_isEngineRunning(rpm) ? (DEFAULT_INJECTION_PRESSURE * 10) : 0;
  jh_store_be16(&txData[3], p);
}

/**
 * @brief Encode fuel tank level percentage for Mode 01 PID 0x2F.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01FuelLevel(uint8_t *txData) {
  txData[0] = 0x03;
  int32_t fuelPercentage =
      (((int32_t)(getGlobalValue(F_FUEL)) * 100) / (FUEL_MIN - FUEL_MAX));
  if (fuelPercentage > 100) {
    fuelPercentage = 100;
  }
  txData[3] = hal_math_percent_to_value(fuelPercentage, 255);
}

/**
 * @brief Encode engine RPM for Mode 01 PID 0x0C.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01EngineRpm(uint8_t *txData) {
  txData[0] = 0x04;
  int32_t engine_Rpm = (int32_t)(getGlobalValue(F_RPM) * 4.0f);
  jh_store_be16(&txData[3], engine_Rpm);
}

/**
 * @brief Encode vehicle speed for Mode 01 PID 0x0D.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01VehicleSpeed(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = (uint8_t)((int32_t)getGlobalValue(F_ABS_CAR_SPEED));
}

/**
 * @brief Encode intake air temperature for Mode 01 PID 0x0F.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01IntakeTemp(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = obd_encodeTempByte(getGlobalValue(F_INTAKE_TEMP));
}

/**
 * @brief Encode the legacy driver-demand signal for Mode 01 throttle-related
 * PIDs.
 * @param txData Output frame buffer.
 * @return None.
 * @note The current ECU reuses generic throttle-related OBD PIDs for the
 * G79/G185-like pedal-demand path because the internal signal is still
 * historically named `F_THROTTLE_POS`.
 */
static void encodeMode01ThrottlePos(uint8_t *txData) {
  txData[0] = 0x03;
  float percent = (getGlobalValue(F_THROTTLE_POS) * 100) / PWM_RESOLUTION;
  txData[3] = hal_math_percent_to_value(percent, 255);
}

/**
 * @brief Encode the ECU's supported OBD standard identifier.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01ObdStandards(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = EOBD_OBD_OBD_II;
}

/**
 * @brief Encode a placeholder engine runtime value.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01EngineRuntime(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = 10;
  txData[4] = 10;
}

/**
 * @brief Encode the supported-PID bitmap for the 0x21-0x40 range.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01Pid_21_40(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x20;
  txData[4] = 0x02;
  txData[5] = 0x00;
  txData[6] = 0x1F;
}

/**
 * @brief Encode catalyst-temperature style data from EGT inputs.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01CatalystTemp(uint8_t *txData) {
  txData[0] = 0x04;
  int32_t temp = ((int32_t)(getGlobalValue(F_EGT)) + 40) * 10;
  jh_store_be16(&txData[3], temp);
}

/**
 * @brief Encode the supported-PID bitmap for the 0x41-0x60 range.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01Pid_41_60(uint8_t *txData) {
  txData[0] = 0x06;
  // 0x46 (Ambient air temperature) is intentionally not advertised,
  // because ECU has only intake temperature input (F_INTAKE_TEMP).
  txData[3] = 0x6B;
  txData[4] = 0xF0;
  txData[5] = 0x80;
  txData[6] = 0xDF;
}

/**
 * @brief Encode ECU supply voltage for Mode 01 PID 0x42.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01EcuVoltage(uint8_t *txData) {
  txData[0] = 0x04;
  int32_t volt = (int32_t)(getGlobalValue(F_VOLTS) * 1000.0f);
  jh_store_be16(&txData[3], volt);
}

/**
 * @brief Encode diesel fuel type for Mode 01 PID 0x51.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01FuelType(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = FUEL_TYPE_DIESEL;
}

/**
 * @brief Encode engine oil temperature for Mode 01 PID 0x5C.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01EngineOilTemp(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = obd_encodeTempByte(getGlobalValue(F_OIL_TEMP));
}

/**
 * @brief Encode a fixed fuel-injection timing value for Mode 01 PID 0x5D.
 * @param txData Output frame buffer.
 * @return None.
 * @note This is a placeholder timing report. Conceptually it is closer to the
 * N108 start-of-injection path than to a measured closed-loop G80/G28 SOI
 * result.
 */
static void encodeMode01FuelTiming(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = 0x61;
  txData[4] = 0x80;
}

/**
 * @brief Encode a fixed fuel-rate value for Mode 01 PID 0x5E.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01FuelRate(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = 0x07;
  txData[4] = 0xD0;
}

/**
 * @brief Encode the configured emissions-standard identifier.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01EmissionsStandard(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = EURO_3;
}

/**
 * @brief Encode the supported-PID bitmap for the 0x61-0x80 range.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01Pid_61_80(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x00;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x11;
}

/**
 * @brief Encode DPF temperature for Mode 01 PID 0x7C.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01DpfTemp(uint8_t *txData) {
  txData[0] = 0x04;
  // PID 0x7C: DPF temperature bank 1, formula = (A*256+B)/10 - 40 °C
  int32_t raw = (int32_t)((getGlobalValue(F_DPF_TEMP) + 40.0f) * 10.0f);
  if (raw < 0) {
    raw = 0;
  }
  if (raw > 65535) {
    raw = 65535;
  }
  jh_store_be16(&txData[3], raw);
}

/**
 * @brief Encode the supported-PID bitmap for the 0x81-0xA0 range.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01Pid_81_A0(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x00;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x01;
}

/**
 * @brief Encode the supported-PID bitmap for the 0xA1-0xC0 range.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01Pid_A1_C0(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x00;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x01;
}

/**
 * @brief Encode the supported-PID bitmap for the 0xC1-0xE0 range.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01Pid_C1_E0(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x00;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x01;
}

/**
 * @brief Encode the supported-PID bitmap for the 0xE1-0xFF range.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01Pid_E1_FF(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x00;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x00;
}

// Fuel temperature is currently exposed through Ford-specific DID DD02, not a
// Mode 01 PID.
static const mode01_pid_handler_t s_mode01PidHandlers[] = {
    {PID_0_20, encodeMode01Pid_00},
    {STATUS_DTC, encodeMode01StatusDtc},
    {FUEL_SYS_STATUS, encodeMode01FuelSysStatus},
    {ENGINE_LOAD, encodeMode01EngineLoad},
    {ABSOLUTE_LOAD, encodeMode01AbsoluteLoad},
    {ENGINE_COOLANT_TEMP, encodeMode01CoolantTemp},
    {FUEL_PRESSURE, encodeMode01FuelPressure},
    {FUEL_RAIL_PRES_ALT, encodeMode01FuelRailPressureAlt},
    {ABS_FUEL_RAIL_PRES, encodeMode01FuelRailPressureAlt},
    {FUEL_LEVEL, encodeMode01FuelLevel},
    {INTAKE_PRESSURE, encodeMode01IntakePressure},
    {ENGINE_RPM, encodeMode01EngineRpm},
    {VEHICLE_SPEED, encodeMode01VehicleSpeed},
    {INTAKE_TEMP, encodeMode01IntakeTemp},
    //(Ambient air temperature) is intentionally not advertised,
    // because ECU has only intake temperature input (F_INTAKE_TEMP).
    {THROTTLE, encodeMode01ThrottlePos},
    {REL_ACCEL_POS, encodeMode01ThrottlePos},
    {REL_THROTTLE_POS, encodeMode01ThrottlePos},
    {ABS_THROTTLE_POS_B, encodeMode01ThrottlePos},
    {ABS_THROTTLE_POS_C, encodeMode01ThrottlePos},
    {ACCEL_POS_D, encodeMode01ThrottlePos},
    {ACCEL_POS_E, encodeMode01ThrottlePos},
    {ACCEL_POS_F, encodeMode01ThrottlePos},
    {COMMANDED_THROTTLE, encodeMode01ThrottlePos},
    {OBDII_STANDARDS, encodeMode01ObdStandards},
    {ENGINE_RUNTIME, encodeMode01EngineRuntime},
    {PID_21_40, encodeMode01Pid_21_40},
    {CAT_TEMP_B1S1, encodeMode01CatalystTemp},
    {CAT_TEMP_B1S2, encodeMode01CatalystTemp},
    {CAT_TEMP_B2S1, encodeMode01CatalystTemp},
    {CAT_TEMP_B2S2, encodeMode01CatalystTemp},
    {PID_41_60, encodeMode01Pid_41_60},
    {ECU_VOLTAGE, encodeMode01EcuVoltage},
    {FUEL_TYPE, encodeMode01FuelType},
    {ENGINE_OIL_TEMP, encodeMode01EngineOilTemp},
    {FUEL_TIMING, encodeMode01FuelTiming},
    {FUEL_RATE, encodeMode01FuelRate},
    {EMISSIONS_STANDARD, encodeMode01EmissionsStandard},
    {PID_61_80, encodeMode01Pid_61_80},
    {P_DPF_TEMP, encodeMode01DpfTemp},
    {PID_81_A0, encodeMode01Pid_81_A0},
    {PID_A1_C0, encodeMode01Pid_A1_C0},
    {PID_C1_E0, encodeMode01Pid_C1_E0},
    {PID_E1_FF, encodeMode01Pid_E1_FF},
};

/**
 * @brief Dispatch one Mode 01 PID request through the encoder table.
 * @param pid Requested PID.
 * @param response CAN response identifier.
 * @param mode Current service mode.
 * @param txData Output frame buffer.
 * @param tx Output flag set when a single-frame response is ready.
 * @return True when the request was fully handled.
 */
static bool handleMode01(uint8_t pid, obd_response_t *response, uint8_t mode,
                         uint8_t *txData, bool *tx) {
  bool handled = false;
  for (size_t i = 0; i < COUNTOF(s_mode01PidHandlers); i++) {
    if (s_mode01PidHandlers[i].pid == pid) {
      s_mode01PidHandlers[i].encoder(txData);
      *tx = true;
      handled = true;
      break;
    }
  }

  if (!handled) {
    obdResponseSetNegative(response, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    unsupportedPrint(mode, pid);
    handled = true;
  }

  return handled;
}

/**
 * @brief Encode only the data bytes for a supported Mode 01 PID.
 * @param pid Requested PID.
 * @param out Output buffer receiving only payload bytes.
 * @param outLen Output pointer receiving payload length.
 * @return True when a PID encoder exists, otherwise false.
 */
bool encodeMode01PidData(uint8_t pid, uint8_t *out, int *outLen) {
  bool encoded = false;
  if (out == NULL || outLen == NULL) {
    return encoded;
  }

  for (size_t i = 0; i < COUNTOF(s_mode01PidHandlers); i++) {
    if (s_mode01PidHandlers[i].pid != pid) {
      continue;
    }

    uint8_t txData[8] = {0};
    s_mode01PidHandlers[i].encoder(txData);

    int dataLen = (int)(txData[0]) - 2; // len includes service + pid
    if (dataLen < 0) {
      dataLen = 0;
    }
    if (dataLen > 4) {
      dataLen = 4;
    }
    (void)memcpy(out, &txData[3], (size_t)dataLen);
    *outLen = dataLen;
    encoded = true;
    break;
  }

  return encoded;
}

/**
 * @brief Handle Mode 06 on-board monitoring requests.
 * @param pid Requested test identifier.
 * @param response CAN response identifier.
 * @param mode Current service mode.
 * @param txData Output frame buffer.
 * @param tx Output flag set when a single-frame response is ready.
 * @return True when the request was fully handled.
 */
static bool handleMode06(uint8_t pid, obd_response_t *response, uint8_t mode,
                         uint8_t *txData, bool *tx) {
  if (pid == 0x00u) { // Supported TIDs 01-20
    txData[0] = 0x06;

    txData[3] = 0x00;
    txData[4] = 0x00;
    txData[5] = 0x00;
    txData[6] = 0x00;
    *tx = true;
  } else {
    obdResponseSetNegative(response, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    unsupportedPrint(mode, pid);
  }
  return true;
}

/**
 * @brief Handle Mode 09 vehicle-information requests.
 * @param pid Requested information PID.
 * @param response CAN response identifier.
 * @param mode Current service mode.
 * @param txData Output frame buffer.
 * @param tx Output flag set when a single-frame response is ready.
 * @return True when the request was fully handled.
 */
static bool handleMode09(uint8_t pid, obd_response_t *response, uint8_t mode,
                         uint8_t *txData, bool *tx) {
  if (pid == 0x00u) { // Supported PIDs 01-20
    txData[0] = 0x06;

    txData[3] = 0x54;
    txData[4] = 0xCA;
    txData[5] = 0x00;
    txData[6] = 0x00;
    *tx = true;
  } else if (pid ==
             (uint8_t)MODE09_PID_VIN) { // VIN (17 to 20 Bytes) Uses ISO-TP
    uint8_t VIN[] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode),
                     pid,
                     0x01,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD,
                     PAD};
    size_t vinLen = strlen(vehicle_Vin);
    if (vinLen > 17u) {
      vinLen = 17u;
    }
    for (size_t a = 0u; a < vinLen; a++) {
      VIN[a + 3u] = (uint8_t)vehicle_Vin[a];
    }
    obdResponseSetPayload(response, sizeof(VIN), VIN);
  } else if (pid ==
             (uint8_t)MODE09_PID_CALID) { // Calibration ID (Ford part number
                                          // format, e.g. XS4A-12A650-AXB)
    // Mode 09 PID 04: fixed 16-byte CALID, null padded.
    uint8_t CID[3 + 16];
    (void)memset(CID, 0, sizeof(CID));
    CID[0] = (uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode);
    CID[1] = pid;
    CID[2] = 0x01u;
    int calLen = (int)strlen(ecu_CalibrationId);
    if (calLen > 16) {
      calLen = 16;
    }
    (void)memcpy(&CID[3], ecu_CalibrationId, (size_t)calLen);
    obdResponseSetPayload(response, sizeof(CID), CID);
  } else if (pid == (uint8_t)MODE09_PID_CVN) { // CVN
    const uint8_t CVN[] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode),
                           pid,
                           0x02,
                           0x11,
                           0x42,
                           0x42,
                           0x42,
                           0x22,
                           0x43,
                           0x43,
                           0x43};
    obdResponseSetPayload(response, sizeof(CVN), CVN);
  } else if (pid ==
             (uint8_t)
                 MODE09_PID_ECU_COUNT) { // ECU name message count for PID 0A.
    txData[0] = 0x03;
    txData[3] = 0x01;
    *tx = true;
  } else if (pid == (uint8_t)MODE09_PID_ECU_NAME) { // ECM Name
    // Mode 09 PID 0A: fixed 20-byte ECU name, null padded.
    uint8_t ECMname[3 + 20];
    (void)memset(ECMname, 0, sizeof(ECMname));
    ECMname[0] = (uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode);
    ECMname[1] = pid;
    ECMname[2] = 0x01u;
    int nameLen = (int)strlen(ecu_Name);
    if (nameLen > 20) {
      nameLen = 20;
    }
    (void)memcpy(&ECMname[3], ecu_Name, (size_t)nameLen);
    obdResponseSetPayload(response, sizeof(ECMname), ECMname);
  } else if (pid == (uint8_t)MODE09_PID_ESN) { // ESN
    const uint8_t ESN[] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode),
                           pid,
                           0x01,
                           0x41,
                           0x72,
                           0x64,
                           0x75,
                           0x69,
                           0x6E,
                           0x6F,
                           0x2D,
                           0x4F,
                           0x42,
                           0x44,
                           0x49,
                           0x49,
                           0x73,
                           0x69,
                           0x6D,
                           0x00};
    obdResponseSetPayload(response, sizeof(ESN), ESN);
  } else if (pid == (uint8_t)MODE09_PID_TYPE_APPR) { // Type Approval Number
    // 20-byte fixed field, null padded.
    uint8_t typeAppr[3 + 20];
    (void)memset(typeAppr, 0, sizeof(typeAppr));
    typeAppr[0] = (uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode);
    typeAppr[1] = pid;
    typeAppr[2] = 0x01u;
    const char *approvalStr = "e11*2005/78*0001*00";
    int aLen = (int)strlen(approvalStr);
    if (aLen > 20) {
      aLen = 20;
    }
    (void)memcpy(&typeAppr[3], approvalStr, (size_t)aLen);
    obdResponseSetPayload(response, sizeof(typeAppr), typeAppr);
  } else {
    obdResponseSetNegative(response, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    unsupportedPrint(mode, pid);
  }

  return true;
}

/**
 * @brief Dispatch one SAE OBD service request.
 * @param mode Requested OBD mode.
 * @param pid Requested PID when applicable.
 * @param response Destination for the application response payload.
 * @return True when the request was recognized and handled.
 */
bool obdJ1979HandleService(uint8_t mode, uint8_t pid,
                           obd_response_t *response) {
  bool handled = false;
  bool tx = false;
  uint8_t txData[] = {0u,  (uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode),
                      pid, PAD,
                      PAD, PAD,
                      PAD, PAD};
  if (mode == (uint8_t)OBD_MODE_CURRENT_DATA) {
    handled = handleMode01(pid, response, mode, txData, &tx);
  } else if ((mode == (uint8_t)OBD_MODE_FREEZE_FRAME) ||
             (mode == (uint8_t)OBD_MODE_O2_MONITORING) ||
             (mode == (uint8_t)OBD_MODE_CONTROL_OPERATIONS)) {
    obdResponseSetNegative(response, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    unsupportedPrint(mode, pid);
    handled = true;
  } else if (mode == (uint8_t)OBD_MODE_STORED_DTC) {
    uint8_t DTCs[24] = {0};
    int dtcLen = fillDtcPayload((uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode),
                                DTC_KIND_STORED, DTCs, sizeof(DTCs));
    obdResponseSetPayload(response, dtcLen, DTCs);
    handled = true;
  } else if (mode == (uint8_t)OBD_MODE_CLEAR_DTC) {
    if (dtcManagerClearAll()) {
      txData[0] = 0x01;
      tx = true;
    } else {
      obdResponseSetNegative(response, mode, NRC_CONDITIONS_NOT_CORRECT);
    }
    handled = true;
  } else if (mode == (uint8_t)OBD_MODE_ONBOARD_MONITORING) {
    handled = handleMode06(pid, response, mode, txData, &tx);
  } else if (mode == (uint8_t)OBD_MODE_PENDING_DTC) {
    uint8_t DTCs[24] = {0};
    int dtcLen = fillDtcPayload((uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode),
                                DTC_KIND_PENDING, DTCs, sizeof(DTCs));
    obdResponseSetPayload(response, dtcLen, DTCs);
    handled = true;
  } else if (mode == (uint8_t)OBD_MODE_VEHICLE_INFO) {
    handled = handleMode09(pid, response, mode, txData, &tx);
  } else if (mode == (uint8_t)OBD_MODE_PERMANENT_DTC) {
    uint8_t DTCs[24] = {0};
    int dtcLen = fillDtcPayload((uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode),
                                DTC_KIND_PERMANENT, DTCs, sizeof(DTCs));
    obdResponseSetPayload(response, dtcLen, DTCs);
    handled = true;
  }

  if (tx) {
    obdResponseSetFrame(response, txData);
  }

  return handled;
}

/**
 * @brief Return the human-readable name of a standard PID.
 * @param pid PID number to look up.
 * @return Static PID description, or "unknown" when out of range.
 */
const char *obdJ1979GetPidName(uint8_t pid) {
  static const char *const pidNames[] = {
      "PIDs supported [01 - 20]",
      "Monitor status since DTCs cleared",
      "Freeze DTC",
      "Fuel system status",
      "Calculated engine load",
      "Engine coolant temperature",
      "Short term fuel trim - Bank 1",
      "Long term fuel trim - Bank 1",
      "Short term fuel trim - Bank 2",
      "Long term fuel trim - Bank 2",
      "Fuel pressure",
      "Intake manifold absolute pressure",
      "Engine RPM",
      "Vehicle speed",
      "Timing advance",
      "Intake air temperature",
      "MAF air flow rate",
      "Throttle position",
      "Commanded secondary air status",
      "Oxygen sensors present (in 2 banks)",
      "Oxygen Sensor 1 - Short term fuel trim",
      "Oxygen Sensor 2 - Short term fuel trim",
      "Oxygen Sensor 3 - Short term fuel trim",
      "Oxygen Sensor 4 - Short term fuel trim",
      "Oxygen Sensor 5 - Short term fuel trim",
      "Oxygen Sensor 6 - Short term fuel trim",
      "Oxygen Sensor 7 - Short term fuel trim",
      "Oxygen Sensor 8 - Short term fuel trim",
      "OBD standards this vehicle conforms to",
      "Oxygen sensors present (in 4 banks)",
      "Auxiliary input status",
      "Run time since engine start",
      "PIDs supported [21 - 40]",
      "Distance traveled with malfunction indicator lamp (MIL) on",
      "Fuel Rail Pressure (relative to manifold vacuum)",
      "Fuel Rail Gauge Pressure (diesel, or gasoline direct injection)",
      "Oxygen Sensor 1 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 2 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 3 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 4 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 5 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 6 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 7 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 8 - Fuel-Air Equivalence Ratio",
      "Commanded EGR",
      "EGR Error",
      "Commanded evaporative purge",
      "Fuel Tank Level Input",
      "Warm-ups since codes cleared",
      "Distance traveled since codes cleared",
      "Evap. System Vapor Pressure",
      "Absolute Barometric Pressure",
      "Oxygen Sensor 1 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 2 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 3 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 4 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 5 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 6 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 7 - Fuel-Air Equivalence Ratio",
      "Oxygen Sensor 8 - Fuel-Air Equivalence Ratio",
      "Catalyst Temperature: Bank 1, Sensor 1",
      "Catalyst Temperature: Bank 2, Sensor 1",
      "Catalyst Temperature: Bank 1, Sensor 2",
      "Catalyst Temperature: Bank 2, Sensor 2",
      "PIDs supported [41 - 60]",
      "Monitor status this drive cycle",
      "Control module voltage",
      "Absolute load value",
      "Fuel-Air commanded equivalence ratio",
      "Relative throttle position",
      "Ambient air temperature",
      "Absolute throttle position B",
      "Absolute throttle position C",
      "Absolute throttle position D",
      "Absolute throttle position E",
      "Absolute throttle position F",
      "Commanded throttle actuator",
      "Time run with MIL on",
      "Time since trouble codes cleared",
      "Maximum value for Fuel-Air equivalence ratio, oxygen sensor voltage, "
      "oxygen sensor current, and intake manifold absolute pressure",
      "Maximum value for air flow rate from mass air flow sensor",
      "Fuel Type",
      "Ethanol fuel percentage",
      "Absolute Evap system Vapor Pressure",
      "Evap system vapor pressure",
      "Short term secondary oxygen sensor trim",
      "Long term secondary oxygen sensor trim",
      "Short term secondary oxygen sensor trim",
      "Long term secondary oxygen sensor trim",
      "Fuel rail absolute pressure",
      "Relative accelerator pedal position",
      "Hybrid battery pack remaining life",
      "Engine oil temperature",
      "Fuel injection timing",
      "Engine fuel rate",
      "Emission requirements to which vehicle is designed",
  };
  const char *name = "unknown";

  if ((size_t)pid < COUNTOF(pidNames)) {
    name = pidNames[pid];
  }

  return name;
}
