
//based on Open-Ecu-Sim-OBD2-FW
//https://github.com/spoonieau/OBD2-ECU-Simulator
// and CAN OBD & UDS Simulator Written By: Cory J. Fowler  December 20th, 2016

#include "obd-2.h"

void obdReq(uint32_t requestId, uint8_t *data);
void negAck(uint32_t responseId, uint8_t mode, uint8_t reason);
static void unsupportedPrint(uint8_t mode, uint8_t pid);
static void unsupportedServicePrint(uint8_t mode);
static void iso_tp(uint32_t responseId, int len, uint8_t *data);
static void iso_tp_process(void);
int fillDtcPayload(uint8_t responseService, dtc_kind_t kind, uint8_t *outData, int maxLen);

static bool requireMinLength(uint32_t responseId, uint8_t serviceId, uint8_t numofBytes, uint8_t minLen);
static uint8_t stMinToMs(uint8_t stMin);
static bool handleMode01(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx);
static bool handleMode06(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx);
static bool handleMode09(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx);
static bool handleObdService(uint8_t mode, uint8_t pid, uint32_t responseId, uint8_t *txData, bool *tx);
static bool handleUdsService(uint8_t mode, uint8_t numofBytes, uint8_t *data, uint32_t responseId, uint8_t *txData, bool *tx);
static bool handleScpPidAccess(uint32_t responseId, uint16_t pid, uint8_t *txData, bool *tx);
static void sendScpGeneralResponse(uint32_t responseId, uint8_t requestMode, uint8_t arg1, uint8_t arg2, uint8_t arg3, uint8_t responseCode);
static void sendScpDmrResponse(uint32_t responseId, uint16_t addr, uint8_t dmrType);

static hal_can_t obdCan = NULL;

// CAN RX Variables
static uint32_t rxId;
static uint8_t dlc;
static uint8_t rxBuf[HAL_CAN_MAX_DATA_LEN];

static uint8_t udsSession = UDS_SESSION_DEFAULT;

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

static iso_tp_ctx_t s_isoTp = {};
static uint32_t s_activeRequestId = LISTEN_ID;

static bool initialized = false;
void obdInit(int retries) {

  for(int a = 0; a < retries; a++) {
    obdCan = hal_can_create(CAN1_GPIO);
    initialized = (obdCan != NULL);
    if(initialized) {
      deb("MCP2515 Initialized Successfully!");
      break;
    }
    derr("Error Initializing MCP2515...");
    hal_delay_ms(SECOND);
    watchdog_feed();
  }

  if(initialized) {
    hal_can_set_std_filters(obdCan, LISTEN_ID, FUNCTIONAL_ID);
    hal_gpio_set_mode(CAN1_INT, HAL_GPIO_INPUT);     // Configuring pin for /INT input
    deb("OBD-2 CAN Shield init ok!");
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, false);
  } else {
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, true);
  }
}

void obdLoop(void) {
  if(!initialized) {
    return;
  }

  iso_tp_process();

  // Block new requests while a multi-frame transfer is in progress.
  if(s_isoTp.state != ISO_TP_IDLE) {
    return;
  }

  if(!hal_gpio_read(CAN1_INT)) {
    if(hal_can_receive(obdCan, &rxId, &dlc, rxBuf)) {
      if(rxId == FUNCTIONAL_ID || rxId == LISTEN_ID) {
        obdReq(rxId, rxBuf);
      }
    }
  }
}

int fillDtcPayload(uint8_t responseService, dtc_kind_t kind, uint8_t *outData, int maxLen) {
  if(outData == NULL || maxLen < 2) {
    return 0;
  }

  uint16_t codes[8];
  uint8_t count = dtcManagerGetCodes(kind, codes, 8);
  int len = 2 + (int)count * 2;
  if(len > maxLen) {
    len = maxLen;
  }

  outData[0] = responseService;
  outData[1] = count;

  int pos = 2;
  for(uint8_t i = 0; i < count && (pos + 1) < maxLen; i++) {
    outData[pos++] = MSB(codes[i]);
    outData[pos++] = LSB(codes[i]);
  }

  return pos;
}

static bool requireMinLength(uint32_t responseId, uint8_t serviceId, uint8_t numofBytes, uint8_t minLen) {
  if(numofBytes < minLen) {
    negAck(responseId, serviceId, NRC_INCORRECT_LENGTH);
    return false;
  }
  return true;
}

static uint8_t stMinToMs(uint8_t stMin) {
  if(stMin <= 0x7F) {
    return stMin;
  }

  // 0xF1..0xF9 are 100us..900us; clamp to 1ms granularity for current scheduler.
  if(stMin >= 0xF1 && stMin <= 0xF9) {
    return 1;
  }

  return 0;
}

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
  if(did == DID_FORD_SW_DATE || did == DID_FORD_CALIBRATION_ID || did == DID_FORD_ROM_SIZE || did == DID_FORD_HARDWARE_ID) {
    return true;
  }
  return false;
}

static bool isFordDiagIdentificationLocalId(uint8_t localId) {
  return (localId == KWP_LID_CALIB_BLOCK || localId == KWP_LID_COMPACT_IDENT || localId == KWP_LID_SUPPORTED_LIST || (localId >= KWP_LID_CALIBRATION_ID && localId <= KWP_LID_COPYRIGHT));
}

static void debugHexPayload(const char *prefix, const uint8_t *buf, int len, int maxBytes) {
  if(prefix == NULL) {
    return;
  }

  if(buf == NULL || len <= 0) {
    deb("%s len=%d", prefix, len);
    return;
  }

  if(maxBytes < 1) maxBytes = 1;
  if(maxBytes > 48) maxBytes = 48;
  int shown = (len < maxBytes) ? len : maxBytes;

  char line[256];
  int pos = snprintf(line, sizeof(line), "%s len=%d bytes:", prefix, len);
  if(pos < 0 || pos >= (int)sizeof(line)) {
    deb("%s len=%d", prefix, len);
    return;
  }

  for(int i = 0; i < shown; i++) {
    int n = snprintf(&line[pos], sizeof(line) - (size_t)pos, " %02X", buf[i]);
    if(n < 0) break;
    pos += n;
    if(pos >= (int)sizeof(line) - 1) {
      break;
    }
  }

  if(shown < len && pos < (int)sizeof(line) - 5) {
    snprintf(&line[pos], sizeof(line) - (size_t)pos, " ...");
  }

  deb("%s", line);
}

typedef void (*mode01_encoder_t)(uint8_t *txData);

typedef struct {
  uint8_t pid;
  mode01_encoder_t encoder;
} mode01_pid_handler_t;

static void encodeMode01Pid_00(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0xB8;
  txData[4] = 0x3A;
  txData[5] = 0x80;
  txData[6] = 0x13;
}

static void encodeMode01StatusDtc(uint8_t *txData) {
  uint8_t activeDTC = dtcManagerCount(DTC_KIND_ACTIVE);
  bool MIL = (activeDTC > 0);
  txData[0] = 0x06;
  txData[3] = (MIL << 7) | (activeDTC & 0x7F);
  txData[4] = 0x07;
  txData[5] = 0xFF;
  txData[6] = 0x00;
}

static void encodeMode01FuelSysStatus(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = 0;
  txData[4] = 0;
}

static void encodeMode01EngineLoad(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = percentToGivenVal(getGlobalValue(F_CALCULATED_ENGINE_LOAD), 255);
}

static void encodeMode01AbsoluteLoad(uint8_t *txData) {
  txData[0] = 0x04;
  int l = percentToGivenVal(getGlobalValue(F_CALCULATED_ENGINE_LOAD), 255);
  txData[3] = MSB(l);
  txData[4] = LSB(l);
}

static void encodeMode01CoolantTemp(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = int(getGlobalValue(F_COOLANT_TEMP) + 40);
}

static void encodeMode01FuelPressure(uint8_t *txData) {
  // PID 0x0A: gauge fuel pressure, 1 byte, kPa = 3*A. Not applicable for diesel VP37.
  txData[0] = 0x03;
  txData[3] = 0;
}

static void encodeMode01FuelRailPressureAlt(uint8_t *txData) {
  txData[0] = 0x04;
  RPM *rpm = getRPMInstance();
  int p = rpm->isEngineRunning() ? (DEFAULT_INJECTION_PRESSURE * 10) : 0;
  txData[3] = MSB(p);
  txData[4] = LSB(p);
}

static void encodeMode01FuelLevel(uint8_t *txData) {
  txData[0] = 0x03;
  int fuelPercentage = ( (int(getGlobalValue(F_FUEL)) * 100) / (FUEL_MIN - FUEL_MAX));
  if(fuelPercentage > 100) {
    fuelPercentage = 100;
  }
  txData[3] = percentToGivenVal(fuelPercentage, 255);
}

static void encodeMode01IntakePressure(uint8_t *txData) {
  txData[0] = 0x03;
  // F_PRESSURE is gauge pressure in bar; OBD PID 0x0B expects absolute kPa.
  int intake_Pressure = (int)((getGlobalValue(F_PRESSURE) + 1.013f) * 100.0f);
  if(intake_Pressure < 0) intake_Pressure = 0;
  if(intake_Pressure > 255) intake_Pressure = 255;
  txData[3] = intake_Pressure;
}

static void encodeMode01EngineRpm(uint8_t *txData) {
  txData[0] = 0x04;
  int engine_Rpm = int(getGlobalValue(F_RPM) * 4);
  txData[3] = MSB(engine_Rpm);
  txData[4] = LSB(engine_Rpm);
}

static void encodeMode01VehicleSpeed(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = int(getGlobalValue(F_ABS_CAR_SPEED));
}

static void encodeMode01IntakeTemp(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = int(getGlobalValue(F_INTAKE_TEMP) + 40);
}

static void encodeMode01ThrottlePos(uint8_t *txData) {
  txData[0] = 0x03;
  float percent = (getGlobalValue(F_THROTTLE_POS) * 100) / PWM_RESOLUTION;
  txData[3] = percentToGivenVal(percent, 255);
}

static void encodeMode01ObdStandards(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = EOBD_OBD_OBD_II;
}

static void encodeMode01EngineRuntime(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = 10;
  txData[4] = 10;
}

static void encodeMode01Pid_21_40(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x20;
  txData[4] = 0x02;
  txData[5] = 0x00;
  txData[6] = 0x1F;
}

static void encodeMode01CatalystTemp(uint8_t *txData) {
  txData[0] = 0x04;
  int temp = (int(getGlobalValue(F_EGT)) + 40) * 10;
  txData[3] = MSB(temp);
  txData[4] = LSB(temp);
}

static void encodeMode01Pid_41_60(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x6F;
  txData[4] = 0xF0;
  txData[5] = 0x80;
  txData[6] = 0xDF;
}

static void encodeMode01EcuVoltage(uint8_t *txData) {
  txData[0] = 0x04;
  int volt = int(getGlobalValue(F_VOLTS) * 1000.0f);
  txData[3] = MSB(volt);
  txData[4] = LSB(volt);
}

static void encodeMode01FuelType(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = FUEL_TYPE_DIESEL;
}

static void encodeMode01EngineOilTemp(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = int(getGlobalValue(F_OIL_TEMP) + 40);
}

static void encodeMode01FuelTiming(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = 0x61;
  txData[4] = 0x80;
}

static void encodeMode01FuelRate(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = 0x07;
  txData[4] = 0xD0;
}

static void encodeMode01EmissionsStandard(uint8_t *txData) {
  txData[0] = 0x03;
  txData[3] = EURO_3;
}

static void encodeMode01Pid_61_80(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x00;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x11;
}

static void encodeMode01DpfTemp(uint8_t *txData) {
  txData[0] = 0x04;
  txData[3] = 0x40;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x00;
}

static void encodeMode01Pid_81_A0(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x00;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x01;
}

static void encodeMode01Pid_A1_C0(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x00;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x01;
}

static void encodeMode01Pid_C1_E0(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x00;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x01;
}

static void encodeMode01Pid_E1_FF(uint8_t *txData) {
  txData[0] = 0x06;
  txData[3] = 0x00;
  txData[4] = 0x00;
  txData[5] = 0x00;
  txData[6] = 0x00;
}

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
  {AMB_AIR_TEMP, encodeMode01IntakeTemp},
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

static bool handleMode01(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx) {
  for(size_t i = 0; i < (sizeof(s_mode01PidHandlers) / sizeof(s_mode01PidHandlers[0])); i++) {
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

// Encode mode 01 PID payload bytes only (without 0x41 and PID).
// Returns true when PID has a registered encoder.
static bool encodeMode01PidData(uint8_t pid, uint8_t *out, int *outLen) {
  if(out == NULL || outLen == NULL) {
    return false;
  }

  for(size_t i = 0; i < (sizeof(s_mode01PidHandlers) / sizeof(s_mode01PidHandlers[0])); i++) {
    if(s_mode01PidHandlers[i].pid != pid) {
      continue;
    }

    uint8_t txData[8] = {0};
    s_mode01PidHandlers[i].encoder(txData);

    int dataLen = int(txData[0]) - 2; // len includes service + pid
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
    uint8_t CVN[] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), pid, 0x02, 0x11, 0x42, 0x42, 0x42, 0x22, 0x43, 0x43, 0x43};
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
    uint8_t ESN[] = {(uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode), pid, 0x01, 0x41, 0x72, 0x64, 0x75, 0x69, 0x6E, 0x6F, 0x2D, 0x4F, 0x42, 0x44, 0x49, 0x49, 0x73, 0x69, 0x6D, 0x00};
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
static void packFieldPad(uint8_t *buf, const char *str, int width, uint8_t pad) {
  int len = (int)strlen(str);
  if(len > width) len = width;
  memcpy(buf, str, (size_t)len);
  for(int i = len; i < width; i++) buf[i] = pad;
}

// Pack a string into a fixed-width null-padded field at buf[0..width-1].
static void packField(uint8_t *buf, const char *str, int width) {
  packFieldPad(buf, str, width, 0x00);
}

// Build and send a UDS 0x22 positive response with a fixed-width ASCII field.
static void send22Field(uint32_t responseId, uint16_t did, const char *str, int width) {
  if(width < 0) width = 0;
  if(width > 40) width = 40;

  uint8_t payload[3 + 40] = {UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF)};
  packField(&payload[3], str, width);
  if(isFordDiagIdentificationDid(did)) {
    deb("UDS 0x22 ident response DID=0x%04X width=%d", did, width);
    debugHexPayload("UDS 0x22 ident resp payload", payload, 3 + width, 36);
  }
  iso_tp(responseId, 3 + width, payload);
}

// Ford identification variant: space-padded (0x20) instead of null-padded.
// Ford EEC-V uses space padding in identification fields.
static void send22IdentField(uint32_t responseId, uint16_t did, const char *str, int width) {
  if(width < 0) width = 0;
  if(width > 40) width = 40;

  uint8_t payload[3 + 40] = {UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF)};
  packFieldPad(&payload[3], str, width, FORD_IDENT_PAD);
  if(isFordDiagIdentificationDid(did)) {
    deb("UDS 0x22 ident response DID=0x%04X width=%d", did, width);
    debugHexPayload("UDS 0x22 ident resp payload", payload, 3 + width, 36);
  }
  iso_tp(responseId, 3 + width, payload);
}

// Build and send a UDS 0x22 response with 32-bit big-endian value.
static void send22U32(uint32_t responseId, uint16_t did, uint32_t value) {
  uint8_t payload[7] = {
    UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
    (uint8_t)((value >> 24) & 0xFF),
    (uint8_t)((value >> 16) & 0xFF),
    (uint8_t)((value >> 8) & 0xFF),
    (uint8_t)(value & 0xFF)
  };
  if(isFordDiagIdentificationDid(did)) {
    deb("UDS 0x22 ident response DID=0x%04X U32=0x%08lX", did, (unsigned long)value);
    debugHexPayload("UDS 0x22 ident resp payload", payload, (int)sizeof(payload), 16);
  }
  iso_tp(responseId, (int)sizeof(payload), payload);
}

// Build and send a KWP 0x12 positive response with a fixed-width ASCII field.
// Uses space padding (0x20) per Ford EEC-V convention.
static void send12LocalField(uint32_t responseId, uint8_t localId, const char *str, int width) {
  if(width < 0) width = 0;
  if(width > 60) width = 60;

  uint8_t payload[2 + 60] = {UDS_RSP_READ_DATA_BY_LOCAL_ID, localId};
  packFieldPad(&payload[2], str, width, FORD_IDENT_PAD);
  debugHexPayload("KWP 0x12 local response", payload, 2 + width, 40);
  iso_tp(responseId, 2 + width, payload);
}

// Build a compact synthetic SCP ID block that can be accessed via service 0x23.
// Layout follows Ford EEC-V conventions documented in CRAI8:
//   Bottom half (0x00-0x7F): non-checksummed identification fields
//   Upper half  (0x80-0xFF): checksummed area with VIN at END_VIN_BYTE_OFFSET=0x95
static void buildScpIdBlock(uint8_t *block, int len) {
  if(block == NULL || len < SCP_IDBLOCK_SIZE) {
    return;
  }

  memset(block, 0x00, (size_t)len);

  // Strategy-defined byte that indicates Flash ID block format (offset +0x13).
  block[0x13] = SCP_IDBLOCK_FMT_DEFAULT;

  // Store commonly requested identification values in a deterministic layout.
  auto writeAscii = [&](int offset, int width, const char *value) {
    if(value == NULL || width <= 0 || offset < 0 || (offset + width) > len) {
      return;
    }

    int n = (int)strlen(value);
    if(n > width) n = width;
    memcpy(&block[offset], value, (size_t)n);
  };

  // Bottom half: identification fields (not in checksummed range)
  writeAscii(0x20, 16, ecu_Model);
  writeAscii(0x30, 8, ecu_Type);
  writeAscii(0x38, 8, ecu_SubType);
  writeAscii(0x40, 8, ecu_CatchCode);
  writeAscii(0x48, 8, ecu_SwDate);
  writeAscii(0x50, 16, ecu_CalibrationId);
  writeAscii(0x60, 16, ecu_PartNumber);
  writeAscii(0x70, 8, ecu_HardwareId);
  block[0x78] = 0x00;
  block[0x79] = 0x08;
  block[0x7A] = 0x00;
  block[0x7B] = 0x00;

  // Upper half: checksummed — VIN at 0x85..0x95, Copyright at 0x97
  writeAscii(SCP_IDBLOCK_VIN_OFFSET, 17, vehicle_Vin);
  writeAscii(SCP_IDBLOCK_COPYRIGHT_OFS, 32, ecu_Copyright);

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

static bool readScpDmrByte(uint8_t dmrType, uint16_t addr, uint8_t *outValue) {
  if(outValue == NULL) {
    return false;
  }

  uint8_t idBlock[SCP_IDBLOCK_SIZE];
  buildScpIdBlock(idBlock, (int)sizeof(idBlock));

  // Common Ford EEEC ID block base addresses (documented examples).
  const uint16_t idStarts[] = {SCP_IDBLOCK_ADDR, SCP_IDBLOCK_ADDR_ALT};
  for(size_t i = 0; i < (sizeof(idStarts) / sizeof(idStarts[0])); i++) {
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

static void sendScpGeneralResponse(uint32_t responseId, uint8_t requestMode, uint8_t arg1, uint8_t arg2, uint8_t arg3, uint8_t responseCode) {
  uint8_t rsp[8] = {0x06, UDS_RSP_NEGATIVE, requestMode, arg1, arg2, arg3, responseCode, PAD};
  hal_can_send(obdCan, responseId, 8, rsp);
}

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
  debugHexPayload("SCP 0x23/0x63 response", rsp, (int)sizeof(rsp), 16);
  hal_can_send(obdCan, responseId, 8, rsp);
}

// Encode a known Ford SCP PID into data bytes.
// Returns the number of data bytes (1/2/4) or 0 if PID is not in the table.
static int encodeFordScpPid(uint16_t pid, uint8_t *out) {
  switch(pid) {
    case SCP_PID_RPM: { // N - Engine RPM, 0.25 rpm resolution, Word
      uint16_t raw = (uint16_t)(getGlobalValue(F_RPM) * 4.0f);
      out[0] = MSB(raw); out[1] = LSB(raw);
      return 2;
    }
    case SCP_PID_VBAT: { // VBAT - Battery Voltage, 0.0625V resolution, Byte
      int raw = (int)(getGlobalValue(F_VOLTS) * 16.0f);
      if(raw < 0) raw = 0;
      if(raw > 255) raw = 255;
      out[0] = (uint8_t)raw;
      return 1;
    }
    case SCP_PID_TP_ENG: { // TP_ENG - Throttle Position A/D, 0.0156 count, Word
      float percent = (getGlobalValue(F_THROTTLE_POS) * 100.0f) / PWM_RESOLUTION;
      if(percent < 0.0f) percent = 0.0f;
      if(percent > 100.0f) percent = 100.0f;
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
      float loadPct = getGlobalValue(F_CALCULATED_ENGINE_LOAD);
      if(loadPct < 0.0f) loadPct = 0.0f;
      if(loadPct > 100.0f) loadPct = 100.0f;
      uint16_t raw = (uint16_t)(loadPct / 100.0f * 32768.0f);
      out[0] = MSB(raw); out[1] = LSB(raw);
      return 2;
    }
    case SCP_PID_VS: { // VS - Vehicle Speed, 0.001953 mph, Word
      float mph = getGlobalValue(F_ABS_CAR_SPEED) * 0.621371f;
      if(mph < 0.0f) mph = 0.0f;
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

static bool handleScpPidAccess(uint32_t responseId, uint16_t pid, uint8_t *txData, bool *tx) {
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

// Ford-specific E3xx identification mapping used by ForDiag.
// Observed behavior: VIN is composed from E301..E305, while E300 is used as Type.
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

static bool handleUdsService(uint8_t mode, uint8_t numofBytes, uint8_t *data, uint32_t responseId, uint8_t *txData, bool *tx) {
  if(mode == UDS_SVC_DIAGNOSTIC_SESSION) {
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }

    uint8_t subFunction = data[2] & 0x7F;
    if(subFunction == UDS_SESSION_DEFAULT || subFunction == UDS_SESSION_PROGRAMMING || subFunction == UDS_SESSION_EXTENDED) {
      udsSession = subFunction;
      uint8_t udsRsp[] = {0x06, UDS_RSP_DIAGNOSTIC_SESSION, subFunction, 0x00, 0x32, 0x01, 0xF4, PAD};
      hal_can_send(obdCan, responseId, 8, udsRsp);
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
    // Most testers send 0x14 + 3-byte groupOfDTC.
    if(!requireMinLength(responseId, mode, numofBytes, 4)) {
      return true;
    }

    dtcManagerClearAll();

    txData[0] = 0x01;
    txData[1] = UDS_RSP_CLEAR_DTC;
    *tx = true;
    return true;
  }

  if(mode == UDS_SVC_READ_DTC_INFO) {
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
    uint16_t addr = (uint16_t(data[3]) << 8) | uint16_t(data[4]);
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

    uint16_t did = (uint16_t(data[2]) << 8) | uint16_t(data[3]);
    deb("UDS 0x22 DID=0x%04X len=%d", did, numofBytes);
    // Detect multi-DID requests: service(1) + N*DID(2) means numofBytes > 3 for N>1.
    if(numofBytes > 3) {
      int didCount = (numofBytes - 1) / 2;
      deb("UDS 0x22 MULTI-DID detected: %d DIDs in request (only 1st processed!)", didCount);
      debugHexPayload("UDS 0x22 multi-DID raw", data, (int)numofBytes + 1, 16);
    }
    if(isFordDiagIdentificationDid(did)) {
      deb("UDS 0x22 ident request DID=0x%04X reqId=0x%03lX", did, (unsigned long)s_activeRequestId);
      debugHexPayload("UDS 0x22 ident request raw", data, (int)numofBytes + 1, 16);
    }
    if(did == DID_VIN) {
      uint8_t payload[] = {
        UDS_RSP_READ_DATA_BY_ID, (uint8_t)(DID_VIN >> 8), (uint8_t)(DID_VIN & 0xFF),
        PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD,
        PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD
      };
      for(int a = 0; a < 17 && a < (int)strlen(vehicle_Vin); a++) {
        payload[3 + a] = uint8_t(vehicle_Vin[a]);
      }
      deb("UDS 0x22 F190 VIN='%s'", vehicle_Vin);
      debugHexPayload("UDS 0x22 F190 payload", payload, 20, 24);
      iso_tp(responseId, 20, payload);
    } else if(did == DID_ACTIVE_SESSION) {
      uint8_t udsRsp[] = {0x04, UDS_RSP_READ_DATA_BY_ID, (uint8_t)(DID_ACTIVE_SESSION >> 8), (uint8_t)(DID_ACTIVE_SESSION & 0xFF), udsSession, PAD, PAD, PAD};
      hal_can_send(obdCan, responseId, 8, udsRsp);
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
    } else if((did & 0xFF00) == 0xF400 && did >= 0xF40A) {
      // ForDiag live-data mirror: DID F4xx -> OBD mode 01 PID xx payload.
      uint8_t pid = (uint8_t)(did & 0x00FF);
      uint8_t dataBytes[4] = {0};
      int dataLen = 0;
      if(encodeMode01PidData(pid, dataBytes, &dataLen)) {
        uint8_t payload[3 + 4] = {UDS_RSP_READ_DATA_BY_ID, (uint8_t)(did >> 8), (uint8_t)did};
        memcpy(&payload[3], dataBytes, (size_t)dataLen);
        iso_tp(responseId, 3 + dataLen, payload);
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
      send22IdentField(responseId, did, ecu_SwDate, 8);
    } else if(did == DID_FORD_CALIBRATION_ID) {
      send22IdentField(responseId, did, ecu_CalibrationId, 16);
    } else if(did == DID_FORD_HARDWARE_ID) {
      send22IdentField(responseId, did, ecu_HardwareId, 8);
    } else if(did == DID_FORD_ROM_SIZE) {
      send22U32(responseId, did, FORD_ROM_SIZE_512K);
    } else if(did == DID_FORD_CATCH_CODE) {
      send22IdentField(responseId, did, ecu_CatchCode, 8);
    } else if(did == DID_FORD_PART_NUMBER) {
      send22IdentField(responseId, did, ecu_PartNumber, 16);
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
    if(isFordDiagIdentificationLocalId(localId)) {
      deb("KWP 0x12 ident request localId=0x%02X reqId=0x%03lX", localId, (unsigned long)s_activeRequestId);
      debugHexPayload("KWP 0x12 ident request raw", data, (int)numofBytes + 1, 16);
    }

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
        uint8_t rsp[] = {UDS_RSP_READ_DATA_BY_LOCAL_ID, localId,
                         (uint8_t)((FORD_ROM_SIZE_512K >> 24) & 0xFF),
                         (uint8_t)((FORD_ROM_SIZE_512K >> 16) & 0xFF),
                         (uint8_t)((FORD_ROM_SIZE_512K >> 8) & 0xFF),
                         (uint8_t)(FORD_ROM_SIZE_512K & 0xFF)};
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
        deb("KWP 0x12/0x33 fields: sw=%s swDate=%s cal=%s", ecu_SwVersion, ecu_SwDate, ecu_CalibrationId);
        debugHexPayload("KWP 0x12/0x33 response", resp, (int)sizeof(resp), 40);
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
        debugHexPayload("KWP 0x12/0xFF response", resp, (int)sizeof(resp), 8);
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

void obdReq(uint32_t requestId, uint8_t *data){
  uint8_t numofBytes = data[0];

  // Log raw hex of every incoming frame for diagnostics.
  debugHexPayload("RX raw", data, (numofBytes <= 8) ? (numofBytes + 1) : 8, 16);

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
  s_activeRequestId = requestId;

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
      hal_can_send(obdCan, responseId, 8, txData);
    }
    return;
  }

  if(handleUdsService(mode, numofBytes, data, responseId, txData, &tx)) {
    if(tx) {
      hal_can_send(obdCan, responseId, 8, txData);
    }
    return;
  }

  // Remaining UDS services are currently not implemented in this dispatcher.
  negAck(responseId, mode, NRC_SERVICE_NOT_SUPPORTED);
  unsupportedServicePrint(mode);
}


// Generic debug serial output
void negAck(uint32_t responseId, uint8_t mode, uint8_t reason){
  uint8_t txData[] = {0x03,UDS_RSP_NEGATIVE,mode,reason,PAD,PAD,PAD,PAD};
  hal_can_send(obdCan, responseId, 8, txData);
}


// Generic debug serial output
static void unsupportedPrint(uint8_t mode, uint8_t pid){
  char msgstring[64];
  snprintf(msgstring, sizeof(msgstring) - 1, "Mode $%02X: Unsupported PID $%02X requested!", mode, pid);
  deb(msgstring);
}


// Generic debug serial output for UDS/OBD service-level rejects.
static void unsupportedServicePrint(uint8_t mode){
  char msgstring[64];
  snprintf(msgstring, sizeof(msgstring) - 1, "Unsupported service $%02X requested!", mode);
  deb(msgstring);
}


// Non-blocking ISO-TP: sends Single Frame directly, or First Frame and arms
// the state machine for Consecutive Frames driven by iso_tp_process().
static void iso_tp(uint32_t responseId, int len, uint8_t *data) {
  if(data == NULL || len <= 0) {
    return;
  }

  if(len <= 7) {
    uint8_t sf[8] = {0};
    sf[0] = (uint8_t)len;
    for(int i = 0; i < len; i++) {
      sf[i + 1] = data[i];
    }
    hal_can_send(obdCan, responseId, 8, sf);
    return;
  }

  int copyLen = (len <= ISO_TP_MAX_PAYLOAD) ? len : ISO_TP_MAX_PAYLOAD;
  if(copyLen != len) {
    // Keep announced length consistent with transmitted data when clamping.
    derr("ISO-TP payload truncated from %d to %d bytes", len, copyLen);
  }
  memcpy(s_isoTp.data, data, (size_t)copyLen);
  s_isoTp.len        = copyLen;
  s_isoTp.offset     = 0;
  s_isoTp.responseId = responseId;
  s_isoTp.requestId  = s_activeRequestId;
  s_isoTp.index      = 1;
  s_isoTp.stMin      = 0;
  s_isoTp.blockSize  = 0;
  s_isoTp.blockSent  = 0;
  s_isoTp.lastCfTime = 0;

  uint8_t tpData[8] = {0};
  tpData[0] = 0x10 | ((copyLen >> 8) & 0x0F);
  tpData[1] = (uint8_t)(copyLen & 0xFF);
  for(uint8_t i = 2; i < 8 && s_isoTp.offset < copyLen; i++) {
    tpData[i] = s_isoTp.data[s_isoTp.offset++];
  }
  hal_can_send(obdCan, responseId, 8, tpData);

  s_isoTp.fcWaitStart = hal_millis();
  s_isoTp.state = ISO_TP_WAIT_FC;
}

// Called every obdLoop() iteration; sends one CF per call, handles FC frames.
static void iso_tp_process(void) {
  if(s_isoTp.state == ISO_TP_IDLE) {
    return;
  }

  if(s_isoTp.state == ISO_TP_WAIT_FC) {
    if((hal_millis() - s_isoTp.fcWaitStart) >= ISO_TP_FC_TIMEOUT_MS) {
      derr("ISO-TP timeout waiting for FC (len=%d)", s_isoTp.len);
      s_isoTp.state = ISO_TP_IDLE;
      return;
    }
    // Drain up to several frames per call so that stale/non-OBD traffic
    // cannot keep the MCP2515 RX buffers occupied while we wait for FC.
    for(int drain = 0; drain < 8; drain++) {
      if(hal_gpio_read(CAN1_INT)) break;            // no more frames
      bool gotFrame = hal_can_receive(obdCan, &rxId, &dlc, rxBuf);
      if(!gotFrame) break;
      bool idMatches = (rxId == s_isoTp.requestId) || (rxId == LISTEN_ID) || (rxId == FUNCTIONAL_ID);
      if(gotFrame && idMatches) {
        if(dlc >= 3 && ((rxBuf[0] & 0xF0) == 0x30)) {
          uint8_t fcType = rxBuf[0] & 0x0F;
          if(fcType == 0x00) {
            s_isoTp.blockSize  = rxBuf[1];
            s_isoTp.stMin      = stMinToMs(rxBuf[2]);
            s_isoTp.blockSent  = 0;
            s_isoTp.lastCfTime = 0;
            s_isoTp.state      = ISO_TP_SEND_CF;
            return;
          } else if(fcType == 0x01) {
            s_isoTp.fcWaitStart = hal_millis(); // extend wait window
            return;
          } else if(fcType == 0x02) {
            derr("ISO-TP FC abort from tester");
            s_isoTp.state = ISO_TP_IDLE;
            return;
          }
        } else {
          // Non-FC frame arrived while waiting for FC - tester sent a new request.
          derr("ISO-TP WAIT_FC: non-FC frame LOST rxId=0x%03lX PCI=0x%02X",
               (unsigned long)rxId, rxBuf[0]);
          debugHexPayload("ISO-TP lost frame", rxBuf, (dlc < 8) ? dlc : 8, 8);
        }
      }
      // Non-matching ID or non-FC: discard and try next frame.
    }
    return;
  }

  // ISO_TP_SEND_CF: send at most one CF per call, respecting stMin.
  if(s_isoTp.lastCfTime != 0 &&
     (hal_millis() - s_isoTp.lastCfTime) < s_isoTp.stMin) {
    return;
  }

  uint8_t tpData[8] = {0};
  tpData[0] = 0x20 | (s_isoTp.index & 0x0F);
  s_isoTp.index = (uint8_t)((s_isoTp.index + 1) & 0x0F);

  for(uint8_t i = 1; i < 8; i++) {
    if(s_isoTp.offset < s_isoTp.len) {
      tpData[i] = s_isoTp.data[s_isoTp.offset++];
    }
  }

  hal_can_send(obdCan, s_isoTp.responseId, 8, tpData);
  s_isoTp.lastCfTime = hal_millis();

  if(s_isoTp.offset >= s_isoTp.len) {
    deb("ISO-TP TX done %d bytes", s_isoTp.len);
    s_isoTp.state = ISO_TP_IDLE;
    return;
  }

  if(s_isoTp.blockSize != 0) {
    s_isoTp.blockSent++;
    if(s_isoTp.blockSent >= s_isoTp.blockSize) {
      s_isoTp.blockSent   = 0;
      s_isoTp.fcWaitStart = hal_millis();
      s_isoTp.state       = ISO_TP_WAIT_FC;
    }
  }
}
