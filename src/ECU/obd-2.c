
#include "obd-2.h"
#include "ecu_unit_testing.h"
#include "rpm.h"
#include "vp37.h"

void obdReq(uint32_t requestId, uint8_t *data);
void negAck(uint32_t responseId, uint8_t mode, uint8_t reason);
static void iso_tp(uint32_t responseId, int len, const uint8_t *data);
static void iso_tp_process(void);
int fillDtcPayload(uint8_t responseService, dtc_kind_t kind, uint8_t *outData, int maxLen);

static bool requireMinLength(uint32_t responseId, uint8_t serviceId, uint8_t numofBytes, uint8_t minLen);
TESTABLE_STATIC uint8_t stMinToMs(uint8_t stMin);
static bool handleMode01(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx);
static bool handleMode06(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx);
static bool handleMode09(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx);
static bool handleObdService(uint8_t mode, uint8_t pid, uint32_t responseId, uint8_t *txData, bool *tx);
static bool handleUdsService(uint8_t mode, uint8_t numofBytes, uint8_t *data, uint32_t responseId, uint8_t *txData, bool *tx);
static bool handleScpPidAccess(uint32_t responseId, uint16_t pid, uint8_t *txData, bool *tx);
static void sendScpGeneralResponse(uint32_t responseId, uint8_t requestMode, uint8_t arg1, uint8_t arg2, uint8_t arg3, uint8_t responseCode);
static void sendScpDmrResponse(uint32_t responseId, uint16_t addr, uint8_t dmrType);
static void unsupportedPrint(uint8_t mode, uint8_t pid);
static void unsupportedServicePrint(uint8_t mode);

#define ISO_TP_MAX_PAYLOAD 160
#define ISO_TP_FC_TIMEOUT_MS 1000

typedef enum {
  ISO_TP_IDLE = 0,
  ISO_TP_WAIT_FC,
  ISO_TP_SEND_CF,
} iso_tp_state_t;

typedef struct {
  uint8_t        data[ISO_TP_MAX_PAYLOAD];
  int            len;
  int            offset;
  uint32_t       responseId;
  uint32_t       requestId;
  uint8_t        index;
  uint8_t        stMin;
  uint8_t        blockSize;
  uint8_t        blockSent;
  unsigned long  fcWaitStart;
  unsigned long  lastCfTime;
  iso_tp_state_t state;
} iso_tp_ctx_t;

typedef struct {
  hal_can_t canHandle;
  uint32_t rxIdValue;
  uint8_t dlcValue;
  uint8_t rxBufValue[HAL_CAN_MAX_DATA_LEN];
  uint8_t udsSessionValue;
  iso_tp_ctx_t isoTpState;
  uint32_t activeRequestIdValue;
  bool initializedFlag;
#ifdef OBD_ENABLE_TOTDIST
  uint32_t totalDistanceKmValue;
#endif
} obd_state_t;

static obd_state_t s_obdState = {
  .canHandle = NULL,
  .rxIdValue = 0u,
  .dlcValue = 0u,
  .rxBufValue = {0u},
  .udsSessionValue = UDS_SESSION_DEFAULT,
  .isoTpState = {
    .data = {0u},
    .len = 0,
    .offset = 0,
    .responseId = 0u,
    .requestId = 0u,
    .index = 0u,
    .stMin = 0u,
    .blockSize = 0u,
    .blockSent = 0u,
    .fcWaitStart = 0uL,
    .lastCfTime = 0uL,
    .state = ISO_TP_IDLE
  },
  .activeRequestIdValue = LISTEN_ID,
  .initializedFlag = false
#ifdef OBD_ENABLE_TOTDIST
  , .totalDistanceKmValue = ecu_TotalDistanceKmDefault
#endif
};

#ifdef OBD_ENABLE_TOTDIST

/**
 * @brief Return the emulated total-distance value for Ford-specific DIDs.
 * @return Odometer value in kilometers.
 */
uint32_t obdGetTotalDistanceKm(void) {
  return s_obdState.totalDistanceKmValue;
}

/**
 * @brief Update the emulated total-distance value for Ford-specific DIDs.
 * @param km New odometer value in kilometers.
 * @return None.
 */
void obdSetTotalDistanceKm(uint32_t km) {
  s_obdState.totalDistanceKmValue = km;
}
#endif

/**
 * @brief Initialize the CAN-based OBD/UDS responder.
 * @param retries Number of CAN initialization retries to request.
 * @return None.
 */
void obdInit(int retries) {

  s_obdState.canHandle = hal_can_create_with_retry(CAN1_GPIO, CAN1_INT, NULL,
                                      retries > 0 ? retries - 1 : 0,
                                      watchdog_feed);
  s_obdState.initializedFlag = (s_obdState.canHandle != NULL);

  if(s_obdState.initializedFlag) {
    hal_can_set_std_filters(s_obdState.canHandle, LISTEN_ID, FUNCTIONAL_ID);
    deb("OBD-2 CAN Shield init ok!");
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, false);
  } else {
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, true);
  }
}

/**
 * @brief Poll CAN for requests and advance any active ISO-TP transmission.
 * @return None.
 */
void obdLoop(void) {
  if(!s_obdState.initializedFlag) {
    return;
  }

  iso_tp_process();

  // Block new requests while a multi-frame transfer is in progress.
  if(s_obdState.isoTpState.state != ISO_TP_IDLE) {
    return;
  }

  if(!hal_gpio_read(CAN1_INT)) {
    if(hal_can_receive(s_obdState.canHandle, &s_obdState.rxIdValue, &s_obdState.dlcValue, s_obdState.rxBufValue)) {
      if(s_obdState.rxIdValue == FUNCTIONAL_ID || s_obdState.rxIdValue == LISTEN_ID) {
        obdReq(s_obdState.rxIdValue, s_obdState.rxBufValue);
      }
    }
  }
}

/**
 * @brief Pack DTC codes into a compact payload for diagnostic responses.
 * @param responseService Positive-response service identifier.
 * @param kind DTC collection to export.
 * @param outData Output buffer receiving the payload.
 * @param maxLen Size of @p outData in bytes.
 * @return Number of bytes written.
 */
int fillDtcPayload(uint8_t responseService, dtc_kind_t kind, uint8_t *outData, int maxLen) {
  if(outData == NULL || maxLen < 2) {
    return 0;
  }

  uint16_t codes[8];
  uint8_t count = dtcManagerGetCodes(kind, codes, 8);

  outData[0] = responseService;
  outData[1] = count;

  int pos = 2;
  for(uint8_t i = 0; i < count && (pos + 1) < maxLen; i++) {
    outData[pos++] = MSB(codes[i]);
    outData[pos++] = LSB(codes[i]);
  }

  return pos;
}

/**
 * @brief Validate minimum request length and send NRC 0x13 on failure.
 * @param responseId CAN identifier used for the negative response.
 * @param serviceId Service currently being processed.
 * @param numofBytes Request length encoded in the incoming frame.
 * @param minLen Minimum accepted payload length.
 * @return True when the request is long enough, otherwise false.
 */
static bool requireMinLength(uint32_t responseId, uint8_t serviceId, uint8_t numofBytes, uint8_t minLen) {
  if(numofBytes < minLen) {
    negAck(responseId, serviceId, NRC_INCORRECT_LENGTH);
    return false;
  }
  return true;
}

/**
 * @brief Convert ISO-TP STmin encoding into scheduler-friendly milliseconds.
 * @param stMin Raw ISO-TP STmin byte.
 * @return Millisecond delay used between consecutive frames.
 */
TESTABLE_STATIC uint8_t stMinToMs(uint8_t stMin) {
  if(stMin <= 0x7F) {
    return stMin;
  }

  // 0xF1..0xF9 are 100us..900us; clamp to 1ms granularity for current scheduler.
  if(stMin >= 0xF1 && stMin <= 0xF9) {
    return 1;
  }

  return 0;
}

#ifdef OBD_VERBOSE_IDENT_DEBUG
/**
 * @brief Check whether a DID belongs to the Ford identification subset.
 * @param did DID to classify.
 * @return True when the DID is part of Ford identification handling.
 */
static bool isFordDiagIdentificationDid(uint16_t did) {
  if(did == DID_FORD_MODEL || did == DID_PART_NUMBER || did == DID_SW_VERSION || did == DID_VIN || did == DID_FORD_CATCH_CODE) {
    return true;
  }
  if(did == DID_F4_MODEL_16 || did == DID_F4_TYPE_ALT || did == DID_F4_SUBTYPE_ALT || did == DID_F4_CATCH_CODE_ALT || did == DID_F4_SW_DATE_ALT
     || did == DID_F4_CALIBRATION_ID_ALT || did == DID_F4_HARDWARE_ID_ALT || did == DID_F4_ROM_SIZE_ALT || did == DID_F4_PART_NUMBER_ALT || did == DID_F4_SW_VERSION
     || did == DID_F4_COPYRIGHT_ALT) {
    return true;
  }
  if(did >= DID_F4_MODEL && did <= DID_F4_COPYRIGHT) {
    return true;
  }
  if(did >= DID_FORD_TYPE && did <= DID_FORD_VIN_CHUNK_LAST) {
    return true;
  }
  if(did == DID_FORD_SW_DATE || did == DID_FORD_PARTNUM_MIDDLE || did == DID_FORD_PARTNUM_SUFFIX || did == DID_FORD_PARTNUM_PREFIX) {
    return true;
  }
  return false;
}

/**
 * @brief Check whether a KWP local ID is used by Ford identification flows.
 * @param localId Local identifier to classify.
 * @return True when the local ID is part of Ford identification handling.
 */
static bool isFordDiagIdentificationLocalId(uint8_t localId) {
  return (localId == KWP_LID_CALIB_BLOCK || localId == KWP_LID_COMPACT_IDENT || localId == KWP_LID_SUPPORTED_LIST || (localId >= KWP_LID_CALIBRATION_ID && localId <= KWP_LID_COPYRIGHT));
}
#endif

typedef void (*mode01_encoder_t)(uint8_t *txData);

typedef struct {
  uint8_t pid;
  mode01_encoder_t encoder;
} mode01_pid_handler_t;

/**
 * @brief Encode the Mode 01 supported-PID bitmap for 0x00-0x20.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01Pid_00(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x98;    // PIDs 01,04,05 (removed 03=FuelSysStatus for diesel)
  txData[4] = 0x3A;    // PIDs 0B,0C,0D,0F
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
  bool MIL = (activeDTC > 0);
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
TESTABLE_STATIC uint8_t obd_encodeTempByte(float tempC) {
  int32_t raw = (int32_t)(tempC + 40.0f);
  if(raw < 0) {
    raw = 0;
  } else if(raw > 255) {
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
  txData[3] = percentToGivenVal(getGlobalValue(F_CALCULATED_ENGINE_LOAD), 255);
}

/**
 * @brief Encode absolute engine load for Mode 01 PID 0x43.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01AbsoluteLoad(uint8_t *txData) {
  txData[0] = 0x04;
  int l = percentToGivenVal(getGlobalValue(F_CALCULATED_ENGINE_LOAD), 255);
  txData[3] = MSB(l);
  txData[4] = LSB(l);
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
  // F_PRESSURE is gauge bar (above atmosphere); convert: kPa_abs = bar*100 + 101
  int32_t kpa = (int32_t)(getGlobalValue(F_PRESSURE) * 100.0f) + 101;
  if(kpa < 0) kpa = 0;
  if(kpa > 255) kpa = 255;
  txData[0] = 0x03;
  txData[3] = (uint8_t)kpa;
}

/**
 * @brief Encode fuel pressure placeholder for Mode 01 PID 0x0A.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01FuelPressure(uint8_t *txData) {
  // PID 0x0A: gauge fuel pressure, 1 byte, kPa = 3*A. Not applicable for diesel VP37.
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
  txData[3] = MSB(p);
  txData[4] = LSB(p);
}

/**
 * @brief Encode fuel tank level percentage for Mode 01 PID 0x2F.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01FuelLevel(uint8_t *txData) {
  txData[0] = 0x03;
  int32_t fuelPercentage = (((int32_t)(getGlobalValue(F_FUEL)) * 100) / (FUEL_MIN - FUEL_MAX));
  if(fuelPercentage > 100) {
    fuelPercentage = 100;
  }
  txData[3] = percentToGivenVal(fuelPercentage, 255);
}

/**
 * @brief Encode engine RPM for Mode 01 PID 0x0C.
 * @param txData Output frame buffer.
 * @return None.
 */
static void encodeMode01EngineRpm(uint8_t *txData) {
  txData[0] = 0x04;
  int32_t engine_Rpm = (int32_t)(getGlobalValue(F_RPM) * 4.0f);
  txData[3] = MSB(engine_Rpm);
  txData[4] = LSB(engine_Rpm);
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
 * @brief Encode the legacy driver-demand signal for Mode 01 throttle-related PIDs.
 * @param txData Output frame buffer.
 * @return None.
 * @note The current ECU reuses generic throttle-related OBD PIDs for the G79/G185-like
 *       pedal-demand path because the internal signal is still historically named
 *       `F_THROTTLE_POS`.
 */
static void encodeMode01ThrottlePos(uint8_t *txData) {
  txData[0] = 0x03;
  float percent = (getGlobalValue(F_THROTTLE_POS) * 100) / PWM_RESOLUTION;
  txData[3] = percentToGivenVal(percent, 255);
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
  txData[3] = MSB(temp);
  txData[4] = LSB(temp);
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
  txData[3] = MSB(volt);
  txData[4] = LSB(volt);
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
 * @note This is a placeholder timing report. Conceptually it is closer to the N108
 *       start-of-injection path than to a measured closed-loop G80/G28 SOI result.
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
  if(raw < 0) raw = 0;
  if(raw > 65535) raw = 65535;
  txData[3] = MSB(raw);
  txData[4] = LSB(raw);
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

// Fuel temperature is currently exposed through Ford-specific DID DD02, not a Mode 01 PID.
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
 * @param responseId CAN response identifier.
 * @param mode Current service mode.
 * @param txData Output frame buffer.
 * @param tx Output flag set when a single-frame response is ready.
 * @return True when the request was fully handled.
 */
static bool handleMode01(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx) {
  for(size_t i = 0; i < COUNTOF(s_mode01PidHandlers); i++) {
    if(s_mode01PidHandlers[i].pid == pid) {
      s_mode01PidHandlers[i].encoder(txData);
      *tx = true;
      return true;
    }
  }

  negAck(responseId, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
  unsupportedPrint(mode, pid);
  return true;

}

/**
 * @brief Encode only the data bytes for a supported Mode 01 PID.
 * @param pid Requested PID.
 * @param out Output buffer receiving only payload bytes.
 * @param outLen Output pointer receiving payload length.
 * @return True when a PID encoder exists, otherwise false.
 */
bool encodeMode01PidData(uint8_t pid, uint8_t *out, int *outLen) {
  if(out == NULL || outLen == NULL) {
    return false;
  }

  for(size_t i = 0; i < COUNTOF(s_mode01PidHandlers); i++) {
    if(s_mode01PidHandlers[i].pid != pid) {
      continue;
    }

    uint8_t txData[8] = {0};
    s_mode01PidHandlers[i].encoder(txData);

    int dataLen = (int)(txData[0]) - 2; // len includes service + pid
    if(dataLen < 0) {
      dataLen = 0;
    }
    if(dataLen > 4) {
      dataLen = 4;
    }
    memcpy(out, &txData[3], (size_t)dataLen);
    *outLen = dataLen;
    return true;
  }

  return false;
}

/**
 * @brief Handle Mode 06 on-board monitoring requests.
 * @param pid Requested test identifier.
 * @param responseId CAN response identifier.
 * @param mode Current service mode.
 * @param txData Output frame buffer.
 * @param tx Output flag set when a single-frame response is ready.
 * @return True when the request was fully handled.
 */
static bool handleMode06(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx) {
  if(pid == 0x00){        // Supported TIDs 01-20
    txData[0] = 0x06;

    txData[3] = 0x00;
    txData[4] = 0x00;
    txData[5] = 0x00;
    txData[6] = 0x00;
    *tx = true;
    return true;
  }

  negAck(responseId, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
  unsupportedPrint(mode, pid);
  return true;
}

/**
 * @brief Handle Mode 09 vehicle-information requests.
 * @param pid Requested information PID.
 * @param responseId CAN response identifier.
 * @param mode Current service mode.
 * @param txData Output frame buffer.
 * @param tx Output flag set when a single-frame response is ready.
 * @return True when the request was fully handled.
 */
static bool handleMode09(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx) {
  if(pid == 0x00){        // Supported PIDs 01-20
    txData[0] = 0x06;

    txData[3] = 0x54;
    txData[4] = 0xCA;
    txData[5] = 0x00;
    txData[6] = 0x00;
    *tx = true;
  }
  else if(pid == MODE09_PID_VIN){    // VIN (17 to 20 Bytes) Uses ISO-TP
    uint8_t VIN[] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), pid, 0x01, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD};
    for(int a = 0; a < (int)strlen(vehicle_Vin); a++) {
      VIN[a + 3] = (uint8_t)vehicle_Vin[a];
    }
    iso_tp(responseId, 20, VIN);
  }
  else if(pid == MODE09_PID_CALID){    // Calibration ID (Ford part number format, e.g. XS4A-12A650-AXB)
    // Mode 09 PID 04: fixed 16-byte CALID, null padded.
    uint8_t CID[3 + 16] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), pid, 0x01};
    int calLen = (int)strlen(ecu_CalibrationId);
    if(calLen > 16) calLen = 16;
    memcpy(&CID[3], ecu_CalibrationId, (size_t)calLen);
    iso_tp(responseId, (int)sizeof(CID), CID);
  }
  else if(pid == MODE09_PID_CVN){    // CVN
    const uint8_t CVN[] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), pid, 0x02, 0x11, 0x42, 0x42, 0x42, 0x22, 0x43, 0x43, 0x43};
    iso_tp(responseId, 11, CVN);
  }
  else if(pid == MODE09_PID_ECU_COUNT){    // ECU name message count for PID 0A.
    txData[0] = 0x03;
    txData[3] = 0x01;
    *tx = true;
  }
  else if(pid == MODE09_PID_ECU_NAME){    // ECM Name
    // Mode 09 PID 0A: fixed 20-byte ECU name, null padded.
    uint8_t ECMname[3 + 20] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), pid, 0x01};
    int nameLen = (int)strlen(ecu_Name);
    if(nameLen > 20) nameLen = 20;
    memcpy(&ECMname[3], ecu_Name, (size_t)nameLen);
    iso_tp(responseId, (int)sizeof(ECMname), ECMname);
  }
  else if(pid == MODE09_PID_ESN){    // ESN
    const uint8_t ESN[] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), pid, 0x01, 0x41, 0x72, 0x64, 0x75, 0x69, 0x6E, 0x6F, 0x2D, 0x4F, 0x42, 0x44, 0x49, 0x49, 0x73, 0x69, 0x6D, 0x00};
    iso_tp(responseId, 20, ESN);
  }
  else if(pid == MODE09_PID_TYPE_APPR){    // Type Approval Number
    // 20-byte fixed field, null padded.
    uint8_t typeAppr[3 + 20] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), pid, 0x01};
    const char *approvalStr = "e11*2005/78*0001*00";
    int aLen = (int)strlen(approvalStr);
    if(aLen > 20) aLen = 20;
    memcpy(&typeAppr[3], approvalStr, (size_t)aLen);
    iso_tp(responseId, (int)sizeof(typeAppr), typeAppr);
  }
  else{
    negAck(responseId, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    unsupportedPrint(mode, pid);
  }

  return true;
}

/**
 * @brief Dispatch one SAE OBD service request.
 * @param mode Requested OBD mode.
 * @param pid Requested PID when applicable.
 * @param responseId CAN response identifier.
 * @param txData Output frame buffer.
 * @param tx Output flag set when a single-frame response is ready.
 * @return True when the request was recognized and handled.
 */
static bool handleObdService(uint8_t mode, uint8_t pid, uint32_t responseId, uint8_t *txData, bool *tx) {
  if(mode == OBD_MODE_CURRENT_DATA) {
    return handleMode01(pid, responseId, mode, txData, tx);
  }

  if(mode == OBD_MODE_FREEZE_FRAME || mode == OBD_MODE_O2_MONITORING || mode == OBD_MODE_CONTROL_OPERATIONS) {
    negAck(responseId, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    unsupportedPrint(mode, pid);
    return true;
  }

  if(mode == OBD_MODE_STORED_DTC) {
    uint8_t DTCs[24] = {0};
    int dtcLen = fillDtcPayload((uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), DTC_KIND_STORED, DTCs, sizeof(DTCs));
    iso_tp(responseId, dtcLen, DTCs);
    return true;
  }

  if(mode == OBD_MODE_CLEAR_DTC) {
    dtcManagerClearAll();
    txData[0] = 0x01;
    *tx = true;
    return true;
  }

  if(mode == OBD_MODE_ONBOARD_MONITORING) {
    return handleMode06(pid, responseId, mode, txData, tx);
  }

  if(mode == OBD_MODE_PENDING_DTC) {
    uint8_t DTCs[24] = {0};
    int dtcLen = fillDtcPayload((uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), DTC_KIND_PENDING, DTCs, sizeof(DTCs));
    iso_tp(responseId, dtcLen, DTCs);
    return true;
  }

  if(mode == OBD_MODE_VEHICLE_INFO) {
    return handleMode09(pid, responseId, mode, txData, tx);
  }

  if(mode == OBD_MODE_PERMANENT_DTC) {
    uint8_t DTCs[24] = {0};
    int dtcLen = fillDtcPayload((uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), DTC_KIND_PERMANENT, DTCs, sizeof(DTCs));
    iso_tp(responseId, dtcLen, DTCs);
    return true;
  }

  return false;
}

// Pack a string into a fixed-width field at buf[0..width-1], using chosen padding.
#define packFieldPad hal_pack_field_pad

// Pack a string into a fixed-width null-padded field at buf[0..width-1].
#define packField    hal_pack_field

/**
 * @brief Send a UDS 0x22 response containing a fixed-width ASCII field.
 * @param responseId CAN response identifier.
 * @param did DID being answered.
 * @param str Source string.
 * @param width Fixed payload width.
 * @return None.
 */
static void send22Field(uint32_t responseId, uint16_t did, const char *str, int width) {
  if(width < 0) width = 0;
  if(width > 40) width = 40;

  uint8_t payload[3 + 40] = {UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF)};
  packField(&payload[3], str, width);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  if(isFordDiagIdentificationDid(did)) {
    deb("UDS 0x22 ident response DID=0x%04X width=%d", did, width);
    hal_deb_hex("UDS 0x22 ident resp payload", payload, 3 + width, 36);
  }
#endif
  iso_tp(responseId, 3 + width, payload);
}

/**
 * @brief Send a Ford-style UDS 0x22 identification field using space padding.
 * @param responseId CAN response identifier.
 * @param did DID being answered.
 * @param str Source string.
 * @param width Fixed payload width.
 * @return None.
 */
static void send22IdentField(uint32_t responseId, uint16_t did, const char *str, int width) {
  if(width < 0) width = 0;
  if(width > 40) width = 40;

  uint8_t payload[3 + 40] = {UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF)};
  packFieldPad(&payload[3], str, width, FORD_IDENT_PAD);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  if(isFordDiagIdentificationDid(did)) {
    deb("UDS 0x22 ident response DID=0x%04X width=%d", did, width);
    hal_deb_hex("UDS 0x22 ident resp payload", payload, 3 + width, 36);
  }
#endif
  iso_tp(responseId, 3 + width, payload);
}

/**
 * @brief Send a UDS 0x22 response containing one 32-bit big-endian value.
 * @param responseId CAN response identifier.
 * @param did DID being answered.
 * @param value Value to encode.
 * @return None.
 */
static void send22U32(uint32_t responseId, uint16_t did, uint32_t value) {
  uint8_t payload[7] = {
    UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
    0, 0, 0, 0
  };
  hal_u32_to_bytes_be(value, &payload[3]);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  if(isFordDiagIdentificationDid(did)) {
    deb("UDS 0x22 ident response DID=0x%04X U32=0x%08lX", did, (unsigned long)value);
    hal_deb_hex("UDS 0x22 ident resp payload", payload, (int)sizeof(payload), 16);
  }
#endif
  iso_tp(responseId, (int)sizeof(payload), payload);
}

// ── Ford Part Number encoding for E217/E21A/E219 identification ──────
// Fordiag reads the Ford part number in three pieces:
//   E21A → ASCII prefix      (e.g. "XS4A")
//   E217 → binary middle     (e.g. "12A650" → {0x12,0x0A,0x06,0x50})
//   E219 → encoded suffix    (e.g. "AXB" → 2-byte Ford encoding)
// It reconstructs PREFIX-MIDDLE-SUFFIX, looks it up in an internal
// database, and fills model/type/subtype/etc. fields from the match.

// Ford part number suffix character set (22 chars, no I/O/Q/W).
static const char FORD_PARTNUM_CHARS[] = "ABCDEFGHJKLMNPRSTUVXYZ";
#define FORD_PARTNUM_CHARSET_LEN 22

/**
 * @brief Look up one Ford suffix character in the Fordiag charset.
 * @param c Character to encode.
 * @return Character index, or -1 when unsupported.
 */
static int fordPartCharIndex(char c) {
  for(int i = 0; i < FORD_PARTNUM_CHARSET_LEN; i++) {
    if(FORD_PARTNUM_CHARS[i] == c) return i;
  }
  return -1;
}

/**
 * @brief Encode one Ford part-number suffix fragment into a single byte.
 * @param s Pointer to the suffix fragment to encode.
 * @param len Number of characters to encode from @p s.
 * @return Encoded suffix byte.
 */
uint8_t fordPartSuffixCharsToByte(const char *s, int len) {
  if(len == 1) {
    int idx = fordPartCharIndex(s[0]);
    return (uint8_t)(idx >= 0 ? idx : 0);
  }
  if(len >= 2) {
    int hi = fordPartCharIndex(s[0]);
    int lo = fordPartCharIndex(s[1]);
    if(hi < 0) hi = 0;
    if(lo < 0) lo = 0;
    return (uint8_t)((hi + 1) * FORD_PARTNUM_CHARSET_LEN + lo);
  }
  return 0;
}

/**
 * @brief Split a Ford part number into prefix, middle and suffix spans.
 * @param pn Ford part-number string.
 * @param prefixOut Output pointer receiving the prefix start.
 * @param prefixLen Output pointer receiving prefix length.
 * @param middleOut Output pointer receiving middle-section start.
 * @param middleLen Output pointer receiving middle-section length.
 * @param suffixOut Output pointer receiving suffix start.
 * @param suffixLen Output pointer receiving suffix length.
 * @return True when the string matches PREFIX-MIDDLE-SUFFIX format.
 */
bool fordPartNumberSplit(const char *pn,
                                const char **prefixOut, int *prefixLen,
                                const char **middleOut, int *middleLen,
                                const char **suffixOut, int *suffixLen) {
  if(pn == NULL) return false;
  const char *dash1 = NULL, *dash2 = NULL;
  for(const char *p = pn; *p; p++) {
    if(*p == '-') {
      if(!dash1)      dash1 = p;
      else if(!dash2) dash2 = p;
    }
  }
  if(!dash1 || !dash2) return false;
  *prefixOut = pn;
  *prefixLen = (int)(dash1 - pn);
  *middleOut = dash1 + 1;
  *middleLen = (int)(dash2 - dash1 - 1);
  *suffixOut = dash2 + 1;
  *suffixLen = (int)strlen(dash2 + 1);
  return true;
}

/**
 * @brief Send DID 0xE217 containing the binary middle section of the part number.
 * @param responseId CAN response identifier.
 * @param did DID being answered.
 * @return None.
 */
static void sendE217PartNumMiddle(uint32_t responseId, uint16_t did) {
  static const uint8_t midBytes[] = {ecu_PartNumMiddleHex};
  int midLen = ecu_PartNumMiddleLen;
  if(midLen > 8) midLen = 8;
  uint8_t payload[3 + 8] = {UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF)};
  memcpy(&payload[3], midBytes, (size_t)midLen);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  deb("UDS 0x22 E217 partnum middle len=%d", midLen);
  hal_deb_hex("UDS 0x22 E217 response", payload, 3 + midLen, 16);
#endif
  iso_tp(responseId, 3 + midLen, payload);
}

/**
 * @brief Send DID 0xE21A containing the ASCII part-number prefix.
 * @param responseId CAN response identifier.
 * @param did DID being answered.
 * @return None.
 */
static void sendE21APartNumPrefix(uint32_t responseId, uint16_t did) {
  const char *prefix, *middle, *suffix;
  int prefixLen, middleLen, suffixLen;
  if(!fordPartNumberSplit(ecu_PartNumber, &prefix, &prefixLen, &middle, &middleLen, &suffix, &suffixLen)) {
    negAck(responseId, UDS_SVC_READ_DATA_BY_ID, NRC_CONDITIONS_NOT_CORRECT);
    return;
  }
  if(prefixLen > 16) prefixLen = 16;
  uint8_t payload[3 + 16] = {UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF)};
  memcpy(&payload[3], prefix, (size_t)prefixLen);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  deb("UDS 0x22 E21A partnum prefix len=%d", prefixLen);
  hal_deb_hex("UDS 0x22 E21A response", payload, 3 + prefixLen, 16);
#endif
  iso_tp(responseId, 3 + prefixLen, payload);
}

/**
 * @brief Send DID 0xE219 containing the Ford-encoded part-number suffix.
 * @param responseId CAN response identifier.
 * @param did DID being answered.
 * @return None.
 */
static void sendE219PartNumSuffix(uint32_t responseId, uint16_t did) {
  const char *prefix, *middle, *suffix;
  int prefixLen, middleLen, suffixLen;
  if(!fordPartNumberSplit(ecu_PartNumber, &prefix, &prefixLen, &middle, &middleLen, &suffix, &suffixLen)) {
    negAck(responseId, UDS_SVC_READ_DATA_BY_ID, NRC_CONDITIONS_NOT_CORRECT);
    return;
  }

  uint8_t leftByte = 0, rightByte = 0;
  if(suffixLen >= 3) {
    // 3-char suffix like "AXB": left="AX"(2 chars), right="B"(1 char)
    leftByte = fordPartSuffixCharsToByte(suffix, 2);
    rightByte = fordPartSuffixCharsToByte(&suffix[2], suffixLen - 2);
  } else if(suffixLen == 2) {
    // 2-char suffix like "FC": left="F"(1 char), right="C"(1 char)
    leftByte = fordPartSuffixCharsToByte(suffix, 1);
    rightByte = fordPartSuffixCharsToByte(&suffix[1], 1);
  } else if(suffixLen == 1) {
    rightByte = fordPartSuffixCharsToByte(suffix, 1);
  }
  // Left byte is always even (doubled) per Ford convention.
  leftByte = (uint8_t)(leftByte * 2);

  uint8_t payload[5] = {
    UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
    leftByte, rightByte
  };
#ifdef OBD_VERBOSE_IDENT_DEBUG
  deb("UDS 0x22 E219 partnum suffix left=0x%02X right=0x%02X", leftByte, rightByte);
  hal_deb_hex("UDS 0x22 E219 response", payload, (int)sizeof(payload), 8);
#endif
  iso_tp(responseId, (int)sizeof(payload), payload);
}

/**
 * @brief Send a KWP 0x12 response with a fixed-width space-padded field.
 * @param responseId CAN response identifier.
 * @param localId Local identifier being answered.
 * @param str Source string.
 * @param width Fixed payload width.
 * @return None.
 */
static void send12LocalField(uint32_t responseId, uint8_t localId, const char *str, int width) {
  if(width < 0) width = 0;
  if(width > 60) width = 60;

  uint8_t payload[2 + 60] = {UDS_RSP_READ_DATA_BY_LOCAL_ID, localId};
  packFieldPad(&payload[2], str, width, FORD_IDENT_PAD);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  hal_deb_hex("KWP 0x12 local response", payload, 2 + width, 40);
#endif
  iso_tp(responseId, 2 + width, payload);
}

/**
 * @brief Copy a string into a fixed window inside an ID block.
 * @param block Destination block buffer.
 * @param len Size of @p block.
 * @param offset Byte offset where the field starts.
 * @param width Fixed field width.
 * @param value String to copy.
 * @return None.
 */
static void writeAsciiField(uint8_t *block,
                            int len,
                            int offset,
                            int width,
                            const char *value) {
  if(block == NULL || value == NULL || width <= 0 || offset < 0 || (offset + width) > len) {
    return;
  }

  int n = (int)strlen(value);
  if(n > width) {
    n = width;
  }

  memcpy(&block[offset], value, (size_t)n);
}

/**
 * @brief Build the synthetic Ford SCP identification block used by DMR reads.
 * @param block Output block buffer.
 * @param len Size of @p block in bytes.
 * @return None.
 */
static void buildScpIdBlock(uint8_t *block, int len) {
  if(block == NULL || len < SCP_IDBLOCK_SIZE) {
    return;
  }

  memset(block, 0x00, (size_t)len);

  // Strategy-defined byte that indicates Flash ID block format (offset +0x13).
  block[0x13] = SCP_IDBLOCK_FMT_DEFAULT;

  // Store commonly requested identification values in a deterministic layout.
  // Bottom half: identification fields (not in checksummed range)
  writeAsciiField(block, len, 0x20, 16, ecu_Model);
  writeAsciiField(block, len, 0x30, 8, ecu_Type);
  writeAsciiField(block, len, 0x38, 8, ecu_SubType);
  writeAsciiField(block, len, 0x40, 8, ecu_CatchCode);
  writeAsciiField(block, len, 0x48, 8, ecu_SwDate);
  writeAsciiField(block, len, 0x50, 16, ecu_CalibrationId);
  writeAsciiField(block, len, 0x60, 16, ecu_PartNumber);
  writeAsciiField(block, len, 0x70, 8, ecu_HardwareId);
  block[0x78] = 0x00;
  block[0x79] = 0x08;
  block[0x7A] = 0x00;
  block[0x7B] = 0x00;

  // Upper half: checksummed — VIN at 0x85..0x95, Copyright at 0x97
  writeAsciiField(block, len, SCP_IDBLOCK_VIN_OFFSET, 17, vehicle_Vin);
  writeAsciiField(block, len, SCP_IDBLOCK_COPYRIGHT_OFS, 32, ecu_Copyright);

  // Vidblock checksum (CRAI8 §35.7.3.2): word-by-word sum of bytes
  // 128..255 must equal 0 (mod 65536). Compute correction word at 0xFE.
  uint32_t sum = 0;
  for(int i = 0x80; i < SCP_IDBLOCK_CHKSUM_OFS; i += 2) {
    sum += ((uint16_t)block[i] << 8) | block[i + 1];
    if(sum > 65535) {
      sum -= 65536;
    }
  }
  uint16_t correction = (uint16_t)((65536 - sum) & 0xFFFF);
  block[SCP_IDBLOCK_CHKSUM_OFS]     = (uint8_t)(correction >> 8);
  block[SCP_IDBLOCK_CHKSUM_OFS + 1] = (uint8_t)(correction & 0xFF);
}

/**
 * @brief Read one byte from the synthetic SCP DMR address space.
 * @param dmrType Ford DMR access type.
 * @param addr Requested address.
 * @param outValue Output pointer receiving the byte value.
 * @return True when the read was handled.
 */
static bool readScpDmrByte(uint8_t dmrType, uint16_t addr, uint8_t *outValue) {
  if(outValue == NULL) {
    return false;
  }

  uint8_t idBlock[SCP_IDBLOCK_SIZE];
  buildScpIdBlock(idBlock, (int)sizeof(idBlock));

  // Common Ford EEEC ID block base addresses (documented examples).
  const uint16_t idStarts[] = {SCP_IDBLOCK_ADDR, SCP_IDBLOCK_ADDR_ALT};
  for(size_t i = 0; i < COUNTOF(idStarts); i++) {
    uint16_t start = idStarts[i];
    if(addr >= start && addr < (uint16_t)(start + SCP_IDBLOCK_SIZE)) {
      *outValue = idBlock[(int)(addr - start)];
      return true;
    }
  }

  // Keep ECU responsive for valid DMR service values even when address is outside mapped blocks.
  (void)dmrType;
  *outValue = 0x00;
  return true;
}

/**
 * @brief Send an SCP-style generic negative response frame.
 * @param responseId CAN response identifier.
 * @param requestMode Original service identifier.
 * @param arg1 First echoed argument byte.
 * @param arg2 Second echoed argument byte.
 * @param arg3 Third echoed argument byte.
 * @param responseCode Response or NRC code.
 * @return None.
 */
static void sendScpGeneralResponse(uint32_t responseId, uint8_t requestMode, uint8_t arg1, uint8_t arg2, uint8_t arg3, uint8_t responseCode) {
  uint8_t rsp[8] = {0x06, UDS_RSP_NEGATIVE, requestMode, arg1, arg2, arg3, responseCode, PAD};
  hal_can_send(s_obdState.canHandle, responseId, 8, rsp);
}

/**
 * @brief Send one 4-byte SCP direct-memory response frame.
 * @param responseId CAN response identifier.
 * @param addr Requested base address.
 * @param dmrType Ford DMR access type.
 * @return None.
 */
static void sendScpDmrResponse(uint32_t responseId, uint16_t addr, uint8_t dmrType) {
  uint8_t b0 = 0;
  uint8_t b1 = 0;
  uint8_t b2 = 0;
  uint8_t b3 = 0;
  readScpDmrByte(dmrType, addr, &b0);
  readScpDmrByte(dmrType, (uint16_t)(addr + 1), &b1);
  readScpDmrByte(dmrType, (uint16_t)(addr + 2), &b2);
  readScpDmrByte(dmrType, (uint16_t)(addr + 3), &b3);

  uint8_t rsp[8] = {0x07, UDS_RSP_READ_MEMORY_BY_ADDR, MSB(addr), LSB(addr), b0, b1, b2, b3};
#ifdef OBD_VERBOSE_IDENT_DEBUG
  hal_deb_hex("SCP 0x23/0x63 response", rsp, (int)sizeof(rsp), 16);
#endif
  hal_can_send(s_obdState.canHandle, responseId, 8, rsp);
}

/**
 * @brief Encode one supported Ford SCP PID into raw response bytes.
 * @param pid Ford SCP PID to encode.
 * @param out Output buffer receiving up to 4 data bytes.
 * @return Number of data bytes written, or 0 when unsupported.
 */
static int encodeFordScpPid(uint16_t pid, uint8_t *out) {
  switch(pid) {
    case SCP_PID_RPM: { // N - Engine RPM, 0.25 rpm resolution, Word
      uint16_t raw = (uint16_t)(getGlobalValue(F_RPM) * 4.0f);
      out[0] = MSB(raw); out[1] = LSB(raw);
      return 2;
    }
    case SCP_PID_VBAT: { // VBAT - Battery Voltage, 0.0625V resolution, Byte
      int32_t raw = hal_constrain((int32_t)(getGlobalValue(F_VOLTS) * 16.0f), 0, 255);
      out[0] = (uint8_t)raw;
      return 1;
    }
    case SCP_PID_TP_ENG: { // TP_ENG - Throttle Position A/D, 0.0156 count, Word
      float percent = hal_constrain((getGlobalValue(F_THROTTLE_POS) * 100.0f) / PWM_RESOLUTION, 0.0f, 100.0f);
      // Ford 10-bit ADC (0-1023) scaled by Bin 6 (×64)
      uint16_t raw = (uint16_t)(percent * 1023.0f / 100.0f * 64.0f);
      out[0] = MSB(raw); out[1] = LSB(raw);
      return 2;
    }
    case SCP_PID_ECT: { // ECT - Engine Coolant Temp, 2°F resolution, Byte Signed
      float tempF = getGlobalValue(F_COOLANT_TEMP) * 1.8f + 32.0f;
      int8_t raw = (int8_t)(tempF / 2.0f);
      out[0] = (uint8_t)raw;
      return 1;
    }
    case SCP_PID_ACT: { // ACT - Air Charge Temp, 2°F resolution, Byte Signed
      float tempF = getGlobalValue(F_INTAKE_TEMP) * 1.8f + 32.0f;
      int8_t raw = (int8_t)(tempF / 2.0f);
      out[0] = (uint8_t)raw;
      return 1;
    }
    case SCP_PID_LAMBSE1:   // VP37: no lambda sensors → NRC
    case SCP_PID_LAMBSE2:
    case SCP_PID_KAMRF1:    // VP37: no adaptive fuel trim → NRC
    case SCP_PID_KAMRF2:
      return 0;
    case SCP_PID_LOAD: { // LOAD - Engine Load, 1/32768 of std air charge, Word
      float loadPct = hal_constrain(getGlobalValue(F_CALCULATED_ENGINE_LOAD), 0.0f, 100.0f);
      uint16_t raw = (uint16_t)(loadPct / 100.0f * 32768.0f);
      out[0] = MSB(raw); out[1] = LSB(raw);
      return 2;
    }
    case SCP_PID_VS: { // VS - Vehicle Speed, 0.001953 mph, Word
      float mph = hal_constrain(getGlobalValue(F_ABS_CAR_SPEED) * 0.621371f, 0.0f, 65535.0f / 512.0f);
      uint16_t raw = (uint16_t)(mph * 512.0f);
      out[0] = MSB(raw); out[1] = LSB(raw);
      return 2;
    }
    case SCP_PID_BP: { // BP - Barometric Pressure, 0.125 inHg, Byte
      // Default sea level ≈ 29.92 inHg → raw = 29.92/0.125 ≈ 239
      out[0] = 239;
      return 1;
    }
    // SCP_PID_IMAF removed — handled above with VMAF/MAF_RATE (return NRC)
    case SCP_PID_RATCH: { // RATCH - throttle ratchet count, 0.0156, Word
      out[0] = 0; out[1] = 0;
      return 2;
    }
    case SCP_PID_NORPM: { // NO - Neutral output RPM (same scale as N), Word
      uint16_t raw = (uint16_t)(getGlobalValue(F_RPM) * 4.0f);
      out[0] = MSB(raw); out[1] = LSB(raw);
      return 2;
    }
    case SCP_PID_IDBLOCK_ADDR: { // idblock_adrs
      out[0] = SCP_IDBLOCK_BANK;
      out[1] = MSB(SCP_IDBLOCK_ADDR);
      out[2] = LSB(SCP_IDBLOCK_ADDR);
      out[3] = SCP_IDBLOCK_FMT_DEFAULT;
      return 4;
    }
    case SCP_PID_SECURITY_STATUS: { // Security Access Status, Byte
      out[0] = 0x00;
      return 1;
    }
    case SCP_PID_PATS_STATUS: { // PATS Status, Byte
      out[0] = 0x00;
      return 1;
    }
    case SCP_PID_TRIP_COUNT: { // TRIP_COUNT - OBDII trip counter, Byte
      out[0] = 0x01;
      return 1;
    }
    case SCP_PID_CODES_COUNT: { // CODES_COUNT - stored DTC count, Byte
      out[0] = dtcManagerCount(DTC_KIND_STORED);
      return 1;
    }
    case SCP_PID_EGRDC:    // VP37: no electronic EGR → NRC
    case SCP_PID_FUELPW1:   // VP37: no individual injector pulsewidth → NRC
      return 0;
    case SCP_PID_VMAF:     // VMAF - MAF voltage (VP37: no MAF sensor)
    case SCP_PID_MAF_RATE:  // j1979_01_10 - MAF rate (VP37: no MAF sensor)
    case SCP_PID_IMAF:      // IMAF - MAF sensor A/D (VP37: no MAF sensor)
      // Return 0 → not handled → NRC, so Fordiag won't show "MAF system".
      return 0;
  }
  return 0;
}

/**
 * @brief Handle a Ford SCP PID access request tunneled over UDS 0x22.
 * @param responseId CAN response identifier.
 * @param pid Requested Ford SCP PID.
 * @param txData Output frame buffer.
 * @param tx Output flag set when a single-frame response is ready.
 * @return True when the PID was handled.
 */
static bool handleScpPidAccess(uint32_t responseId, uint16_t pid, uint8_t *txData, bool *tx) {
  (void)responseId;  // Intentionally unused; kept for API consistency
  
  if(txData == NULL || tx == NULL) {
    return false;
  }

  uint8_t dataBytes[4] = {0};
  int dataLen = 0;

  // Try known Ford SCP PIDs with proper encoding.
  dataLen = encodeFordScpPid(pid, dataBytes);

  // Unknown PIDs: return false → caller sends NRC.
  // Returning positive zero responses for unknown DIDs caused Fordiag
  // to show "MAP system / MAF system" (any positive = "sensor exists").
  if(dataLen == 0) {
    return false;
  }

  int pci = 3 + dataLen; // 0x62 + DID_H + DID_L + data
  if(pci > 7) pci = 7;
  txData[0] = (uint8_t)pci;
  txData[1] = UDS_RSP_READ_DATA_BY_ID;
  txData[2] = MSB(pid);
  txData[3] = LSB(pid);
  for(int i = 0; i < 4; i++) {
    txData[4 + i] = (i < dataLen) ? dataBytes[i] : PAD;
  }
  *tx = true;
  return true;
}

/**
 * @brief Answer Ford E3xx identification DIDs used by Fordiag.
 * @param responseId CAN response identifier.
 * @param did Requested DID.
 * @return None.
 */
static void send22FordDiagE3xx(uint32_t responseId, uint16_t did) {
  if(did == DID_FORD_TYPE) {
    // Null-padded: Fordiag treats E3xx as a VIN block and space-padding here
    // causes trailing 0x20 bytes to bleed into the VIN display as leading spaces.
    send22Field(responseId, did, ecu_Type, 8);
    return;
  }

  if(did < DID_FORD_VIN_CHUNK_BASE || did > DID_FORD_VIN_CHUNK_LAST) {
    return;
  }

  static const uint8_t chunkOffset[5] = {0, 3, 6, 9, 12};
  static const uint8_t chunkLen[5]    = {3, 3, 3, 3, 5};

  uint8_t idx = (uint8_t)(did - DID_FORD_VIN_CHUNK_BASE);
  int off = (int)chunkOffset[idx];
  int width = (int)chunkLen[idx];
  int vinLen = (int)strlen(vehicle_Vin);

  char vinChunk[6] = {0};
  if(off < vinLen) {
    int copyLen = width;
    if(off + copyLen > vinLen) {
      copyLen = vinLen - off;
    }
    memcpy(vinChunk, &vehicle_Vin[off], (size_t)copyLen);
  }

  deb("UDS 0x22 DID=0x%04X VIN chunk[%u]=%s", did, idx, vinChunk);
  send22Field(responseId, did, vinChunk, width);
}

/**
 * @brief Dispatch one UDS or KWP service request.
 * @param mode Requested service identifier.
 * @param numofBytes Request length encoded in the incoming frame.
 * @param data Raw request buffer.
 * @param responseId CAN response identifier.
 * @param txData Output frame buffer.
 * @param tx Output flag set when a single-frame response is ready.
 * @return True when the service was recognized and handled.
 */
static bool handleUdsService(uint8_t mode, uint8_t numofBytes, uint8_t *data, uint32_t responseId, uint8_t *txData, bool *tx) {
  if(mode == UDS_SVC_DIAGNOSTIC_SESSION) {
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }

    uint8_t subFunction = data[2] & 0x7F;
    if(subFunction == UDS_SESSION_DEFAULT || subFunction == UDS_SESSION_PROGRAMMING || subFunction == UDS_SESSION_EXTENDED) {
      s_obdState.udsSessionValue = subFunction;
      uint8_t udsRsp[] = {0x06, UDS_RSP_DIAGNOSTIC_SESSION, subFunction, 0x00, 0x32, 0x01, 0xF4, PAD};
      hal_can_send(s_obdState.canHandle, responseId, 8, udsRsp);
    } else {
      negAck(responseId, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    }
    return true;
  }

  if(mode == UDS_SVC_ECU_RESET) {
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }

    uint8_t subFunction = data[2] & 0x7F;
    txData[0] = 0x02;
    txData[1] = UDS_RSP_ECU_RESET;
    txData[2] = subFunction;
    *tx = true;
    return true;
  }

  if(mode == UDS_SVC_CLEAR_DTC) {
    // ISO 14229 request is 0x14 + 3-byte groupOfDTC, but some testers
    // send shortened variants (for example only service byte). For
    // compatibility we accept all variants and clear all DTCs.
    if(numofBytes >= 4) {
      uint32_t group = ((uint32_t)data[2] << 16)
                     | ((uint32_t)data[3] << 8)
                     | (uint32_t)data[4];
      deb("UDS 0x14 clearDTC group=0x%06lX", (unsigned long)group);
    } else if(numofBytes >= 2) {
      uint32_t group = 0;
      for(uint8_t i = 2; i <= numofBytes; i++) {
        group = (group << 8) | (uint32_t)data[i];
      }
      deb("UDS 0x14 clearDTC shortGroup bytes=%u value=0x%06lX",
          (unsigned)(numofBytes - 1), (unsigned long)group);
    } else {
      deb("UDS 0x14 clearDTC (no group bytes)");
    }

    dtcManagerClearAll();

    txData[0] = 0x01;
    txData[1] = UDS_RSP_CLEAR_DTC;
    *tx = true;
    return true;
  }

  if(mode == KWP_SVC_READ_DTC_BY_STATUS) {
    // KWP2000 readDiagnosticTroubleCodesByStatus (ISO 14230-3)
    // Request: 18 statusOfDTC groupHi groupLo
    if(!requireMinLength(responseId, mode, numofBytes, 4)) {
      return true;
    }

    uint8_t statusOfDtc = data[2];
    uint16_t group = ((uint16_t)data[3] << 8) | (uint16_t)data[4];
    (void)group; // FF00/FFFF = powertrain/all; we report all regardless

    // statusOfDtc is a bitmask: 0x00 = report all, 0x01 = testFailed (active),
    // 0x08 = confirmedDTC (stored). Treat 0x00 as "all stored".
    dtc_kind_t kind = DTC_KIND_STORED;
    if(statusOfDtc & 0x01) {
      kind = DTC_KIND_ACTIVE;
    }

    uint16_t codes[8] = {0};
    uint8_t count = dtcManagerGetCodes(kind, codes, 8);
    deb("KWP 0x18 statusOfDtc=0x%02X group=0x%04X kind=%d count=%u",
        statusOfDtc, group, (int)kind, count);

    // Response (ISO 14230-3): 58 numberOfDTC [dtcHi dtcLo statusOfDTC]...
    uint8_t payload[40] = {0};
    int p = 0;
    payload[p++] = KWP_RSP_READ_DTC_BY_STATUS;
    payload[p++] = count;

    for(uint8_t i = 0; i < count && (p + 2) < (int)sizeof(payload); i++) {
      payload[p++] = MSB(codes[i]);
      payload[p++] = LSB(codes[i]);
      payload[p++] = (kind == DTC_KIND_ACTIVE) ? 0x01 : 0x08;
    }

    iso_tp(responseId, p, payload);
    return true;
  }

  if(mode == UDS_SVC_READ_DTC_INFO) {
#ifdef FORDIAG_COMPAT_NO_UDS_DTC
    // Fordiag author: "after connect do not response to command 19 = not support UDS".
    // Returning NRC here tells Fordiag the ECU is not a UDS ECU,
    // steering it toward the EEC-V identification path (E217/E21A/E219).
    // DTCs remain accessible via standard OBD Modes 0x03/0x07/0x0A.
    negAck(responseId, mode, NRC_SERVICE_NOT_SUPPORTED);
    return true;
#endif
    // Accept 1-byte "probe" requests: Fordiag sends just service 0x19
    // to check DTC support before the full identification sequence.
    if(numofBytes < 2) {
      uint8_t probe[] = {UDS_RSP_READ_DTC_INFO, 0x02, 0x2F};
      iso_tp(responseId, (int)sizeof(probe), probe);
      return true;
    }

    uint8_t subFunction = data[2];
    deb("UDS 0x19 subFunction=0x%02X len=0x%02X", subFunction, numofBytes);
    if(subFunction == 0x02) {
      uint8_t statusMask = (numofBytes > 3) ? data[3] : 0xFF;
      uint16_t activeCodes[8] = {0};
      uint8_t count = dtcManagerGetCodes(DTC_KIND_ACTIVE, activeCodes, 8);
      deb("UDS 0x19 reportDTCByStatusMask mask=0x%02X activeCount=%u", statusMask, count);

      uint8_t payload[40] = {0};
      int p = 0;
      payload[p++] = UDS_RSP_READ_DTC_INFO;
      payload[p++] = 0x02;
      payload[p++] = 0x2F; // supported status mask

      for(uint8_t i = 0; i < count && (p + 3) < (int)sizeof(payload); i++) {
        uint8_t dtcStatus = 0x01; // testFailed
        if((dtcStatus & statusMask) == 0) {
          continue;
        }

        payload[p++] = 0x00;
        payload[p++] = MSB(activeCodes[i]);
        payload[p++] = LSB(activeCodes[i]);
        payload[p++] = dtcStatus;
      }

      iso_tp(responseId, p, payload);
      return true;
    }

    if(subFunction == 0x0A) {
      // reportSupportedDTC
      uint8_t storedCount = dtcManagerCount(DTC_KIND_STORED);
      deb("UDS 0x19 reportSupportedDTC storedCount=%u", storedCount);
      txData[0] = 0x04;
      txData[1] = UDS_RSP_READ_DTC_INFO;
      txData[2] = 0x0A;
      txData[3] = storedCount;
      *tx = true;
      return true;
    }

    negAck(responseId, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    return true;
  }

  if(mode == UDS_SVC_READ_MEMORY_BY_ADDR) {
    // Ford SCP-style Request DMR Access
    if(!requireMinLength(responseId, mode, numofBytes, 4)) {
      return true;
    }

    uint8_t dmrType = data[2];
    uint16_t addr = ((uint16_t)(data[3]) << 8) | (uint16_t)(data[4]);
    deb("SCP 0x23 DMR type=0x%02X addr=0x%04X", dmrType, addr);

    bool validType = (dmrType == 0x00 || dmrType == 0x01 || dmrType == 0x08 || dmrType == 0x09);
    if(!validType) {
      sendScpGeneralResponse(responseId, UDS_SVC_READ_MEMORY_BY_ADDR, data[2], data[3], data[4], NRC_SUBFUNCTION_NOT_SUPPORTED);
      return true;
    }

    sendScpDmrResponse(responseId, addr, dmrType);
    return true;
  }

  if(mode == UDS_SVC_READ_DATA_BY_ID) {
    if(!requireMinLength(responseId, mode, numofBytes, 3)) {
      return true;
    }

    uint16_t did = ((uint16_t)(data[2]) << 8) | (uint16_t)(data[3]);
    deb("UDS 0x22 DID=0x%04X len=%d", did, numofBytes);
    // Detect multi-DID requests: service(1) + N*DID(2) means numofBytes > 3 for N>1.
    if(numofBytes > 3) {
#ifdef OBD_VERBOSE_IDENT_DEBUG
      int didCount = (numofBytes - 1) / 2;
      deb("UDS 0x22 MULTI-DID detected: %d DIDs in request (only 1st processed!)", didCount);
      hal_deb_hex("UDS 0x22 multi-DID raw", data, (int)numofBytes + 1, 16);
#endif
    }
#ifdef OBD_VERBOSE_IDENT_DEBUG
    if(isFordDiagIdentificationDid(did)) {
      deb("UDS 0x22 ident request DID=0x%04X reqId=0x%03lX", did, (unsigned long)s_obdState.activeRequestIdValue);
      hal_deb_hex("UDS 0x22 ident request raw", data, (int)numofBytes + 1, 16);
    }
#endif
    if(did == DID_VIN) {
      uint8_t payload[] = {
        UDS_RSP_READ_DATA_BY_ID, (uint8_t)(DID_VIN >> 8), (uint8_t)(DID_VIN & 0xFF),
        PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD,
        PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD
      };
      for(int a = 0; a < 17 && a < (int)strlen(vehicle_Vin); a++) {
        payload[3 + a] = (uint8_t)(vehicle_Vin[a]);
      }
#ifdef OBD_VERBOSE_IDENT_DEBUG
      deb("UDS 0x22 F190 VIN='%s'", vehicle_Vin);
      hal_deb_hex("UDS 0x22 F190 payload", payload, 20, 24);
#endif
      iso_tp(responseId, 20, payload);
    } else if(did == DID_ACTIVE_SESSION) {
      uint8_t udsRsp[] = {0x04, UDS_RSP_READ_DATA_BY_ID, (uint8_t)(DID_ACTIVE_SESSION >> 8), (uint8_t)(DID_ACTIVE_SESSION & 0xFF), s_obdState.udsSessionValue, PAD, PAD, PAD};
      hal_can_send(s_obdState.canHandle, responseId, 8, udsRsp);
    } else if(did == DID_SPARE_PART_NUMBER) {
      send22Field(responseId, did, ecu_PartNumber, (int)strlen(ecu_PartNumber));
    } else if(did == DID_SW_VERSION) {
      // Fordiag maps this DID as "SW version".
      send22Field(responseId, did, ecu_SwVersion, 4);
    } else if(did == DID_SW_VERSION_ALT) {
      send22Field(responseId, did, ecu_SwVersion, (int)strlen(ecu_SwVersion));
    } else if(did == DID_SUPPLIER_ID) {
      // System supplier identifier
      const char *supplier = "FORD EEC-V";
      send22Field(responseId, did, supplier, (int)strlen(supplier));
    } else if(did == DID_MANUFACTURE_DATE) {
      send22Field(responseId, did, ecu_SwDate, (int)strlen(ecu_SwDate));
    } else if(did == DID_SERIAL_NUMBER) {
      send22Field(responseId, did, vehicle_Vin, (int)strlen(vehicle_Vin));
    } else if(did == DID_HW_VERSION) {
      send22Field(responseId, did, ecu_HardwareId, (int)strlen(ecu_HardwareId));
    } else if(did == DID_SYSTEM_NAME) {
      // System name / engine type
      const char *sysName = "FORD 1.8 TDDI VP37";
      send22Field(responseId, did, sysName, (int)strlen(sysName));
    } else if(did == DID_ODX_FILE_ID) {
      send22Field(responseId, did, ecu_Model, (int)strlen(ecu_Model));
    } else if(did == DID_FORD_MODEL) {
      send22IdentField(responseId, did, ecu_Model, 8);
    } else if((did >> 8) == (DID_F4_MODEL >> 8) && did > DID_F4_COPYRIGHT) {
      // F4xx live-data mirror takes priority over ALT ident DIDs.
      // ALT ident DIDs (F40B, F40C, …F449) share the same address space,
      // so we must try live-data encoding first to avoid e.g. 0xF40F returning
      // the catch-code string instead of PID 0x0F (IAT) temperature data.
      uint8_t pid = (uint8_t)(did & 0x00FF);
      uint8_t dataBytes[4] = {0};
      int dataLen = 0;
      if(encodeMode01PidData(pid, dataBytes, &dataLen)) {
        uint8_t payload[3 + 4] = {UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)did};
        memcpy(&payload[3], dataBytes, (size_t)dataLen);
        iso_tp(responseId, 3 + dataLen, payload);
      } else if(did == DID_F4_MODEL_16) {
        send22IdentField(responseId, did, ecu_Model, 16);
      } else if(did == DID_F4_TYPE_ALT) {
        send22IdentField(responseId, did, ecu_Type, 8);
      } else if(did == DID_F4_SUBTYPE_ALT) {
        send22IdentField(responseId, did, ecu_SubType, 8);
      } else if(did == DID_F4_CATCH_CODE_ALT) {
        send22IdentField(responseId, did, ecu_CatchCode, 8);
      } else if(did == DID_F4_SW_DATE_ALT) {
        send22IdentField(responseId, did, ecu_SwDate, 8);
      } else if(did == DID_F4_CALIBRATION_ID_ALT) {
        send22IdentField(responseId, did, ecu_CalibrationId, 16);
      } else if(did == DID_F4_HARDWARE_ID_ALT) {
        send22IdentField(responseId, did, ecu_HardwareId, 8);
      } else if(did == DID_F4_ROM_SIZE_ALT) {
        send22U32(responseId, did, FORD_ROM_SIZE_512K);
      } else if(did == DID_F4_PART_NUMBER_ALT) {
        send22IdentField(responseId, did, ecu_PartNumber, 16);
      } else if(did == DID_F4_SW_VERSION) {
        send22IdentField(responseId, did, ecu_SwVersion, 4);
      } else if(did == DID_F4_COPYRIGHT_ALT) {
        send22IdentField(responseId, did, ecu_Copyright, 16);
      } else {
        // Keep alive with a deterministic zero payload for unknown F4xx PIDs.
        uint8_t payload[] = {UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)did, 0x00};
        iso_tp(responseId, (int)sizeof(payload), payload);
      }
    } else if(did >= DID_F4_MODEL && did <= DID_F4_COPYRIGHT) {
      // Ford EEC-V identification DIDs used by identification screen.
      const char *val;
      int width;
      switch(did) {
        case DID_F4_MODEL:          val = ecu_Model;         width = 8;  break;
        case DID_F4_TYPE:           val = ecu_Type;          width = 8;  break;
        case DID_F4_SUBTYPE:        val = ecu_SubType;       width = 8;  break;
        case DID_F4_CATCH_CODE:     val = ecu_CatchCode;     width = 8;  break;
        case DID_F4_SW_DATE:        val = ecu_SwDate;        width = 8;  break;
        case DID_F4_CALIBRATION_ID: val = ecu_CalibrationId; width = 16; break;
        case DID_F4_PART_NUMBER:    val = ecu_PartNumber;    width = 16; break;
        case DID_F4_HARDWARE_ID:    val = ecu_HardwareId;    width = 8;  break;
        case DID_F4_ROM_SIZE:       send22U32(responseId, did, FORD_ROM_SIZE_512K); return true;
        case DID_F4_COPYRIGHT:      val = ecu_Copyright;     width = 16; break;
        default:                    val = ecu_SwVersion;     width = 4;  break;
      }
      send22IdentField(responseId, did, val, width);
    } else if(did == DID_PART_NUMBER) {
      // Fordiag maps this DID as "Part number".
      send22Field(responseId, did, ecu_PartNumber, 16);
    } else if(did == DID_BOOT_SW_ID) {
      send22Field(responseId, did, ecu_SwVersion, 4);
    } else if(did >= DID_FORD_TYPE && did <= DID_FORD_VIN_CHUNK_LAST) {
      send22FordDiagE3xx(responseId, did);
    } else if(did == DID_FORD_SW_DATE) {
      // E200: Fordiag decodes SW date as 3 binary bytes (NOT ASCII!):
      //   byte 0 = month (lower nibble), byte 1 = day, byte 2 = year-1900.
      // Per Fordiag author's ECU_ReadSWVersion FoxPro source.
      // Parse ecu_SwDate "YYYYMMDD" → binary {month, day, year-1900}.
      {
        int year = 0, month = 0, day = 0;
        if(strlen(ecu_SwDate) >= 8) {
          year  = (ecu_SwDate[0]-'0')*1000 + (ecu_SwDate[1]-'0')*100 +
                  (ecu_SwDate[2]-'0')*10   + (ecu_SwDate[3]-'0');
          month = (ecu_SwDate[4]-'0')*10   + (ecu_SwDate[5]-'0');
          day   = (ecu_SwDate[6]-'0')*10   + (ecu_SwDate[7]-'0');
        }
        uint8_t payload[6] = {
          UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
          (uint8_t)(month & 0x0F),
          (uint8_t)day,
          (uint8_t)(year - 1900)
        };
        deb("UDS 0x22 E200 SW date: %d-%02d-%02d → {0x%02X,0x%02X,0x%02X}",
            year, month, day, payload[3], payload[4], payload[5]);
        iso_tp(responseId, (int)sizeof(payload), payload);
      }
    } else if(did == DID_FORD_PARTNUM_MIDDLE) {
      // E217: Fordiag reads binary middle bytes of Ford part number
      // (e.g. "12A650" → {0x12, 0x0A, 0x06, 0x50}) for ECU identification.
      sendE217PartNumMiddle(responseId, did);
    } else if(did == DID_FORD_PARTNUM_PREFIX) {
      // E21A: Fordiag reads ASCII prefix of Ford part number
      // (e.g. "XS4A") for ECU identification.
      sendE21APartNumPrefix(responseId, did);
    } else if(did == DID_FORD_PARTNUM_SUFFIX) {
      // E219: Fordiag reads 2-byte encoded suffix of Ford part number
      // (e.g. "AXB" → {0x52, 0x01}) for ECU identification.
      sendE219PartNumSuffix(responseId, did);
    } else if(did == DID_FORD_CATCH_CODE) {
      send22IdentField(responseId, did, ecu_CatchCode, 8);
    } else if(did == DID_FORD_PART_NUMBER) {
      send22IdentField(responseId, did, ecu_PartNumber, 16);
    } else if(did == DID_FORD_TOTDIST) {
#ifdef OBD_ENABLE_TOTDIST
      // DD01: Total distance (odometer), 3 bytes big-endian unsigned km.
      // Per Fordiag author: "3bytova!" — unusual 3-byte format.
      uint32_t km = obdGetTotalDistanceKm();
      uint8_t payload[6] = {
        UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
        (uint8_t)((km >> 16) & 0xFF),
        (uint8_t)((km >> 8) & 0xFF),
        (uint8_t)(km & 0xFF)
      };
      deb("UDS 0x22 DD01 TOTDIST=%lu km", (unsigned long)km);
      iso_tp(responseId, (int)sizeof(payload), payload);
#else
      negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
#endif
    } else if(did == DID_FORD_OUTTMP) {
      // DD05: External temperature, 1 byte unsigned, value = raw - 40 °C.
      // ECU has no outside temp sensor; use intake temp as best proxy.
      uint8_t raw = obd_encodeTempByte(getGlobalValue(F_INTAKE_TEMP));
      uint8_t payload[4] = {
        UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
        raw
      };
      deb("UDS 0x22 DD05 OUTTMP raw=%u (%.1f°C)", (unsigned)raw, getGlobalValue(F_INTAKE_TEMP));
      iso_tp(responseId, (int)sizeof(payload), payload);
    } else if(did == DID_FORD_FUEL_TEMP) {
      // DD02: Fuel temperature, 1 byte unsigned, value = raw - 40 °C.
      uint8_t raw = obd_encodeTempByte(getGlobalValue(F_FUEL_TEMP));
      uint8_t payload[4] = {
        UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
        raw
      };
      iso_tp(responseId, (int)sizeof(payload), payload);
    } else if(did == DID_FORD_OIL_PRESSURE) {
      // DD03: Oil pressure, 2 bytes big-endian, kPa × 10.
      int32_t raw = hal_constrain((int32_t)(getGlobalValue(F_OIL_PRESSURE) * 10.0f), 0, 65535);
      uint8_t payload[5] = {
        UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
        (uint8_t)(raw >> 8), (uint8_t)(raw & 0xFF)
      };
      iso_tp(responseId, (int)sizeof(payload), payload);
    } else if(did == DID_FORD_BOOST) {
      // DD04: Boost/intake pressure, 2 bytes big-endian, bar × 1000.
      int32_t raw = hal_constrain((int32_t)(getGlobalValue(F_PRESSURE) * 1000.0f), 0, 65535);
      uint8_t payload[5] = {
        UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
        (uint8_t)(raw >> 8), (uint8_t)(raw & 0xFF)
      };
      iso_tp(responseId, (int)sizeof(payload), payload);
    } else if(did == DID_FORD_DPF_PRESSURE) {
      // DD06: DPF differential pressure, 2 bytes big-endian, Pa.
      int32_t raw = hal_constrain((int32_t)(getGlobalValue(F_DPF_PRESSURE) * 1000.0f), 0, 65535);
      uint8_t payload[5] = {
        UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
        (uint8_t)(raw >> 8), (uint8_t)(raw & 0xFF)
      };
      iso_tp(responseId, (int)sizeof(payload), payload);
    } else if(did == DID_FORD_BOOST_DESIRED) {
      // DD07: Desired boost pressure, 2 bytes big-endian, bar × 1000.
      int32_t raw = hal_constrain((int32_t)(getGlobalValue(F_PRESSURE_DESIRED) * 1000.0f), 0, 65535);
      uint8_t payload[5] = {
        UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
        (uint8_t)(raw >> 8), (uint8_t)(raw & 0xFF)
      };
      iso_tp(responseId, (int)sizeof(payload), payload);
    } else if(did == DID_FORD_BOOST_PERCENT) {
      // DD08: Boost duty cycle percentage, 1 byte 0-100.
      int32_t pct = hal_constrain((int32_t)getGlobalValue(F_PRESSURE_PERCENTAGE), 0, 100);
      uint8_t payload[4] = {
        UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
        (uint8_t)pct
      };
      iso_tp(responseId, (int)sizeof(payload), payload);
    } else if(did == DID_ECU_CAPABILITIES) {
      // DID 0x0200 collides with SCP_PID_CODES_COUNT — must return NRC here
      // so Fordiag falls back to Mode 01 PID 0x51 for diesel type detection.
      // A positive response causes Fordiag to show "MAP system / MAF system".
      negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
    } else if(handleScpPidAccess(responseId, did, txData, tx)) {
      // Compatibility with legacy Ford SCP "REQUEST_PID_ACCESS" style over CAN.
      // Positive response already prepared in txData.
    } else {
      negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
    }
    return true;
  }

  if(mode == UDS_SVC_READ_DATA_BY_LOCAL_ID) {
    // KWP2000 service 0x12 - ReadDataByLocalIdentifier
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }

    uint8_t localId = data[2];
    deb("KWP 0x12 localId=0x%02X", localId);
#ifdef OBD_VERBOSE_IDENT_DEBUG
    if(isFordDiagIdentificationLocalId(localId)) {
      deb("KWP 0x12 ident request localId=0x%02X reqId=0x%03lX", localId, (unsigned long)s_obdState.activeRequestIdValue);
      hal_deb_hex("KWP 0x12 ident request raw", data, (int)numofBytes + 1, 16);
    }
#endif

    switch(localId) {
      case KWP_LID_CALIBRATION_ID:  send12LocalField(responseId, localId, ecu_CalibrationId, 16); return true;
      case KWP_LID_SW_DATE:          send12LocalField(responseId, localId, ecu_SwDate, 8);         return true;
      case KWP_LID_PART_NUMBER:      send12LocalField(responseId, localId, ecu_PartNumber, 16);    return true;
      case KWP_LID_MODEL_16:         send12LocalField(responseId, localId, ecu_Model, 16);         return true;
      case KWP_LID_VIN:              send12LocalField(responseId, localId, vehicle_Vin, 17);       return true;
      case KWP_LID_MODEL:            send12LocalField(responseId, localId, ecu_Model, 16);         return true;
      case KWP_LID_TYPE:             send12LocalField(responseId, localId, ecu_Type, 8);           return true;
      case KWP_LID_SUBTYPE:          send12LocalField(responseId, localId, ecu_SubType, 8);        return true;
      case KWP_LID_CATCH_CODE:       send12LocalField(responseId, localId, ecu_CatchCode, 8);      return true;
      case KWP_LID_VIN_ALT:          send12LocalField(responseId, localId, vehicle_Vin, 17);       return true;
      case KWP_LID_SW_VERSION:       send12LocalField(responseId, localId, ecu_SwVersion, 4);      return true;
      case KWP_LID_SW_DATE_ALT:      send12LocalField(responseId, localId, ecu_SwDate, 8);         return true;
      case KWP_LID_CALIBRATION_ALT:  send12LocalField(responseId, localId, ecu_CalibrationId, 16); return true;
      case KWP_LID_PART_NUMBER_ALT:  send12LocalField(responseId, localId, ecu_PartNumber, 16);    return true;
      case KWP_LID_HARDWARE_ID:      send12LocalField(responseId, localId, ecu_HardwareId, 8);     return true;
      case KWP_LID_COPYRIGHT:        send12LocalField(responseId, localId, ecu_Copyright, 32);     return true;
      case KWP_LID_ROM_SIZE: {
        // ROM size: 512 KB = 0x00080000
        uint8_t rsp[6] = {UDS_RSP_READ_DATA_BY_LOCAL_ID, localId};
        hal_u32_to_bytes_be(FORD_ROM_SIZE_512K, &rsp[2]);
        iso_tp(responseId, (int)sizeof(rsp), rsp);
        return true;
      }

      // ---- ForDiag EEC-V identification blocks -------------------------
      case KWP_LID_CALIB_BLOCK: {
        // Software calibration block used by Fordiag:
        // SwVersion(4) + SwDate(8) + CalibId(16)
        uint8_t resp[2 + 16 + 4 + 8];
        resp[0] = UDS_RSP_READ_DATA_BY_LOCAL_ID; resp[1] = localId;
        // Ford EEC-V convention: space-padded ASCII fields.
        packFieldPad(&resp[2],  ecu_SwVersion,       4, FORD_IDENT_PAD);
        packFieldPad(&resp[6],  ecu_SwDate,          8, FORD_IDENT_PAD);
        packFieldPad(&resp[14], ecu_CalibrationId,  16, FORD_IDENT_PAD);
#ifdef OBD_VERBOSE_IDENT_DEBUG
        deb("KWP 0x12/0x33 fields: sw=%s swDate=%s cal=%s", ecu_SwVersion, ecu_SwDate, ecu_CalibrationId);
        hal_deb_hex("KWP 0x12/0x33 response", resp, (int)sizeof(resp), 40);
#endif
        iso_tp(responseId, (int)sizeof(resp), resp);
        return true;
      }
      case KWP_LID_COMPACT_IDENT:
        // Disabled: our custom compact block format doesn't match what Fordiag
        // expects, causing it to show "MAP system / MAF system" instead of
        // "DIESEL engine". NRC forces fallback to standard PID 0x51 detection.
        negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
        return true;
      case KWP_LID_SUPPORTED_LIST: {
        // Supported local identifiers list used by some scan tools to discover ID fields.
        uint8_t resp[] = {
          UDS_RSP_READ_DATA_BY_LOCAL_ID, localId,
          KWP_LID_CALIB_BLOCK,
          KWP_LID_CALIBRATION_ID, KWP_LID_SW_DATE, KWP_LID_PART_NUMBER, KWP_LID_MODEL_16,
          KWP_LID_VIN, KWP_LID_MODEL, KWP_LID_TYPE, KWP_LID_SUBTYPE, KWP_LID_CATCH_CODE, KWP_LID_VIN_ALT,
          KWP_LID_SW_VERSION, KWP_LID_SW_DATE_ALT, KWP_LID_CALIBRATION_ALT, KWP_LID_PART_NUMBER_ALT, KWP_LID_HARDWARE_ID, KWP_LID_ROM_SIZE, KWP_LID_COPYRIGHT
        };
#ifdef OBD_VERBOSE_IDENT_DEBUG
        hal_deb_hex("KWP 0x12/0xFF response", resp, (int)sizeof(resp), 8);
#endif
        iso_tp(responseId, (int)sizeof(resp), resp);
        return true;
      }
      // ------------------------------------------------------------------

      default:
        negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
        return true;
    }

    // Unreachable because all accepted IDs return in the switch above.
    return true;
  }

  if(mode == UDS_SVC_TESTER_PRESENT) {
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }

    uint8_t subFunction = data[2];

    // 0x80 means suppress positive response.
    if((subFunction & UDS_SUPPRESS_POSITIVE_RSP) != 0) {
      return true;
    }

    // Accept any sub-function value (Fordiag sends 0x01 in addition to 0x00).
    txData[0] = 0x02;
    txData[1] = UDS_RSP_TESTER_PRESENT;
    txData[2] = (uint8_t)(subFunction & 0x7F);
    *tx = true;
    return true;
  }

  // ── Services listed in Ford ISO-15765 table but not fully implemented ──
  // Respond with proper NRC so Ford diagnostic tools see the ECU as aware
  // of these services rather than treating it as a communication failure.

  if(mode == UDS_SVC_SECURITY_ACCESS) {
    // SecurityAccess: no seed/key implemented on this emulated ECU.
    negAck(responseId, mode, NRC_CONDITIONS_NOT_CORRECT);
    return true;
  }

  if(mode == UDS_SVC_COMM_CONTROL) {
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }
    uint8_t subFunction = data[2] & 0x7F;
    if(subFunction == 0x00) {
      // enableRxAndTx — acknowledge default state.
      txData[0] = 0x02;
      txData[1] = UDS_RSP_COMM_CONTROL;
      txData[2] = subFunction;
      *tx = true;
    } else {
      negAck(responseId, mode, NRC_CONDITIONS_NOT_CORRECT);
    }
    return true;
  }

  if(mode == UDS_SVC_WRITE_DATA_BY_ID) {
    // No writable DIDs on this ECU.
    negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
    return true;
  }

  if(mode == UDS_SVC_IO_CONTROL) {
    // No controllable I/O on this ECU.
    negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
    return true;
  }

  if(mode == UDS_SVC_ROUTINE_CONTROL) {
    // No supported routines.
    negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
    return true;
  }

  if(mode == UDS_SVC_CONTROL_DTC_SETTING) {
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }
    uint8_t subFunction = data[2] & 0x7F;
    // Accept both DTCSettingOn (0x01) and DTCSettingOff (0x02).
    if(subFunction == 0x01 || subFunction == 0x02) {
      txData[0] = 0x02;
      txData[1] = UDS_RSP_CONTROL_DTC_SETTING;
      txData[2] = subFunction;
      *tx = true;
    } else {
      negAck(responseId, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    }
    return true;
  }

  return false;
}

/**
 * @brief Parse and answer one incoming OBD/UDS CAN request frame.
 * @param requestId CAN identifier of the incoming request.
 * @param data Raw request buffer.
 * @return None.
 */
void obdReq(uint32_t requestId, uint8_t *data){
  uint8_t numofBytes = data[0];

#ifdef OBD_VERBOSE_RX_DEBUG
  hal_deb_hex("RX raw", data, (numofBytes <= 8) ? (numofBytes + 1) : 8, 16);
#endif

  // Ignore ISO-TP control/segmentation frames arriving as normal OBD requests.
  if(numofBytes > 8) {
    deb("RX dropped: ISO-TP multi-frame PCI=0x%02X (no MF rx support)", data[0]);
    return;
  }
  if(numofBytes < 1) {
    return;
  }

  uint32_t responseId = REPLY_ID;
  if(requestId == LISTEN_ID) {
    responseId = REPLY_ID;
  }
  s_obdState.activeRequestIdValue = requestId;

  uint8_t mode = data[1];
  uint8_t pid = (numofBytes > 1) ? data[2] : 0;
  bool tx = false;
  uint8_t txData[] = {0x00,(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode),pid,PAD,PAD,PAD,PAD,PAD};

  if(mode == OBD_MODE_CURRENT_DATA && pid <= PID_LAST) {
    deb("OBD-2 pid:0x%02x (%s) length:0x%02x mode:0x%02x",  pid, getPIDName(pid), numofBytes, mode);
  } else {
    deb("OBD/UDS service:0x%02x length:0x%02x reqId=0x%03lX", mode, numofBytes, (unsigned long)requestId);
  }
  
  if(handleObdService(mode, pid, responseId, txData, &tx)) {
    if(tx) {
      hal_can_send(s_obdState.canHandle, responseId, 8, txData);
    }
    return;
  }

  if(handleUdsService(mode, numofBytes, data, responseId, txData, &tx)) {
    if(tx) {
      hal_can_send(s_obdState.canHandle, responseId, 8, txData);
    }
    return;
  }

  // Remaining UDS services are currently not implemented in this dispatcher.
  negAck(responseId, mode, NRC_SERVICE_NOT_SUPPORTED);
  unsupportedServicePrint(mode);
}


/**
 * @brief Send a generic UDS negative response frame.
 * @param responseId CAN response identifier.
 * @param mode Original service identifier.
 * @param reason Negative-response code.
 * @return None.
 */
void negAck(uint32_t responseId, uint8_t mode, uint8_t reason){
  uint8_t txData[] = {0x03,UDS_RSP_NEGATIVE,mode,reason,PAD,PAD,PAD,PAD};
  hal_can_send(s_obdState.canHandle, responseId, 8, txData);
}


/**
 * @brief Log an unsupported PID request.
 * @param mode Requested service mode.
 * @param pid Unsupported PID.
 * @return None.
 */
static void unsupportedPrint(uint8_t mode, uint8_t pid){
  deb("Mode $%02X: Unsupported PID $%02X requested!", mode, pid);
}


/**
 * @brief Log an unsupported service request.
 * @param mode Unsupported service identifier.
 * @return None.
 */
static void unsupportedServicePrint(uint8_t mode){
  deb("Unsupported service $%02X requested!", mode);
}


/**
 * @brief Start a non-blocking ISO-TP response transmission.
 * @param responseId CAN response identifier.
 * @param len Number of payload bytes to send.
 * @param data Payload bytes to transmit.
 * @return None.
 */
static void iso_tp(uint32_t responseId, int len, const uint8_t *data) {
  if(data == NULL || len <= 0) {
    return;
  }

  if(len <= 7) {
    uint8_t sf[8] = {0};
    sf[0] = (uint8_t)len;
    for(int i = 0; i < len; i++) {
      sf[i + 1] = data[i];
    }
    hal_can_send(s_obdState.canHandle, responseId, 8, sf);
    return;
  }

  int copyLen = (len <= ISO_TP_MAX_PAYLOAD) ? len : ISO_TP_MAX_PAYLOAD;
  if(copyLen != len) {
    // Keep announced length consistent with transmitted data when clamping.
    derr("ISO-TP payload truncated from %d to %d bytes", len, copyLen);
  }
  memcpy(s_obdState.isoTpState.data, data, (size_t)copyLen);
  s_obdState.isoTpState.len        = copyLen;
  s_obdState.isoTpState.offset     = 0;
  s_obdState.isoTpState.responseId = responseId;
  s_obdState.isoTpState.requestId  = s_obdState.activeRequestIdValue;
  s_obdState.isoTpState.index      = 1;
  s_obdState.isoTpState.stMin      = 0;
  s_obdState.isoTpState.blockSize  = 0;
  s_obdState.isoTpState.blockSent  = 0;
  s_obdState.isoTpState.lastCfTime = 0;

  uint8_t tpData[8] = {0};
  tpData[0] = 0x10 | ((copyLen >> 8) & 0x0F);
  tpData[1] = (uint8_t)(copyLen & 0xFF);
  for(uint8_t i = 2; i < 8 && s_obdState.isoTpState.offset < copyLen; i++) {
    tpData[i] = s_obdState.isoTpState.data[s_obdState.isoTpState.offset++];
  }
  hal_can_send(s_obdState.canHandle, responseId, 8, tpData);

  s_obdState.isoTpState.fcWaitStart = hal_millis();
  s_obdState.isoTpState.state = ISO_TP_WAIT_FC;
}

/**
 * @brief Advance the ISO-TP transmit state machine by one scheduler step.
 * @return None.
 */
static void iso_tp_process(void) {
  if(s_obdState.isoTpState.state == ISO_TP_IDLE) {
    return;
  }

  if(s_obdState.isoTpState.state == ISO_TP_WAIT_FC) {
    if((hal_millis() - s_obdState.isoTpState.fcWaitStart) >= ISO_TP_FC_TIMEOUT_MS) {
      derr("ISO-TP timeout waiting for FC (len=%d)", s_obdState.isoTpState.len);
      s_obdState.isoTpState.state = ISO_TP_IDLE;
      return;
    }
    // Drain up to several frames per call so that stale/non-OBD traffic
    // cannot keep the MCP2515 RX buffers occupied while we wait for FC.
    for(int drain = 0; drain < 8; drain++) {
      if(hal_gpio_read(CAN1_INT)) break;            // no more frames
      bool gotFrame = hal_can_receive(s_obdState.canHandle, &s_obdState.rxIdValue, &s_obdState.dlcValue, s_obdState.rxBufValue);
      if(!gotFrame) break;
      bool idMatches = (s_obdState.rxIdValue == s_obdState.isoTpState.requestId) || (s_obdState.rxIdValue == LISTEN_ID) || (s_obdState.rxIdValue == FUNCTIONAL_ID);
      if(gotFrame && idMatches) {
        if(s_obdState.dlcValue >= 3 && ((s_obdState.rxBufValue[0] & 0xF0) == 0x30)) {
          uint8_t fcType = s_obdState.rxBufValue[0] & 0x0F;
          if(fcType == 0x00) {
            s_obdState.isoTpState.blockSize  = s_obdState.rxBufValue[1];
            s_obdState.isoTpState.stMin      = stMinToMs(s_obdState.rxBufValue[2]);
            s_obdState.isoTpState.blockSent  = 0;
            s_obdState.isoTpState.lastCfTime = 0;
            s_obdState.isoTpState.state      = ISO_TP_SEND_CF;
            return;
          } else if(fcType == 0x01) {
            s_obdState.isoTpState.fcWaitStart = hal_millis(); // extend wait window
            return;
          } else if(fcType == 0x02) {
            derr("ISO-TP FC abort from tester");
            s_obdState.isoTpState.state = ISO_TP_IDLE;
            return;
          }
        } else {
          // Non-FC frame arrived while waiting for FC - tester sent a new request.
          derr("ISO-TP WAIT_FC: non-FC frame LOST s_obdState.rxIdValue=0x%03lX PCI=0x%02X",
               (unsigned long)s_obdState.rxIdValue, s_obdState.rxBufValue[0]);
          hal_deb_hex("ISO-TP lost frame", s_obdState.rxBufValue, (s_obdState.dlcValue < 8) ? s_obdState.dlcValue : 8, 8);
        }
      }
      // Non-matching ID or non-FC: discard and try next frame.
    }
    return;
  }

  // ISO_TP_SEND_CF: send at most one CF per call, respecting stMin.
  if(s_obdState.isoTpState.lastCfTime != 0 &&
     (hal_millis() - s_obdState.isoTpState.lastCfTime) < s_obdState.isoTpState.stMin) {
    return;
  }

  uint8_t tpData[8] = {0};
  tpData[0] = 0x20 | (s_obdState.isoTpState.index & 0x0F);
  s_obdState.isoTpState.index = (uint8_t)((s_obdState.isoTpState.index + 1) & 0x0F);

  for(uint8_t i = 1; i < 8; i++) {
    if(s_obdState.isoTpState.offset < s_obdState.isoTpState.len) {
      tpData[i] = s_obdState.isoTpState.data[s_obdState.isoTpState.offset++];
    }
  }

  hal_can_send(s_obdState.canHandle, s_obdState.isoTpState.responseId, 8, tpData);
  s_obdState.isoTpState.lastCfTime = hal_millis();

  if(s_obdState.isoTpState.offset >= s_obdState.isoTpState.len) {
    deb("ISO-TP TX done %d bytes", s_obdState.isoTpState.len);
    s_obdState.isoTpState.state = ISO_TP_IDLE;
    return;
  }

  if(s_obdState.isoTpState.blockSize != 0) {
    s_obdState.isoTpState.blockSent++;
    if(s_obdState.isoTpState.blockSent >= s_obdState.isoTpState.blockSize) {
      s_obdState.isoTpState.blockSent   = 0;
      s_obdState.isoTpState.fcWaitStart = hal_millis();
      s_obdState.isoTpState.state       = ISO_TP_WAIT_FC;
    }
  }
}
