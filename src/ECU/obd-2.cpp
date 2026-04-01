
//based on Open-Ecu-Sim-OBD2-FW
//https://github.com/spoonieau/OBD2-ECU-Simulator
// and CAN OBD & UDS Simulator Written By: Cory J. Fowler  December 20th, 2016

#include "obd-2.h"

// UDS negative response codes used by this dispatcher.
#define NRC_SERVICE_NOT_SUPPORTED      0x11
#define NRC_SUBFUNCTION_NOT_SUPPORTED  0x12
#define NRC_INCORRECT_LENGTH           0x13
#define NRC_REQUEST_OUT_OF_RANGE       0x31

void obdReq(uint32_t requestId, uint8_t *data);
void unsupported(uint32_t responseId, uint8_t mode, uint8_t pid);
void unsupportedPrint(uint8_t mode, uint8_t pid);
void unsupportedServicePrint(uint8_t mode);
static void iso_tp(uint32_t responseId, int len, uint8_t *data);
static void iso_tp_process(void);
void negAck(uint32_t responseId, uint8_t mode, uint8_t reason);
int fillDtcPayload(uint8_t responseService, dtc_kind_t kind, uint8_t *outData, int maxLen);

static bool requireMinLength(uint32_t responseId, uint8_t serviceId, uint8_t numofBytes, uint8_t minLen);
static uint8_t stMinToMs(uint8_t stMin);
static bool handleMode01(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx);
static bool handleMode06(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx);
static bool handleMode09(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx);
static bool handleObdService(uint8_t mode, uint8_t pid, uint32_t responseId, uint8_t *txData, bool *tx);
static bool handleUdsService(uint8_t mode, uint8_t numofBytes, uint8_t *data, uint32_t responseId, uint8_t *txData, bool *tx);

static hal_can_t obdCan = NULL;

// CAN RX Variables
static uint32_t rxId;
static uint8_t dlc;
static uint8_t rxBuf[HAL_CAN_MAX_DATA_LEN];

static uint8_t udsSession = 0x01; // default session

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

void storeECUName(uint8_t *tab, int idx) {
  for(int a = 0; a < (int)strlen(ecu_Name); a++) {
    tab[a + idx] = (uint8_t)ecu_Name[a];
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
  txData[0] = 0x04;
  RPM *rpm = getRPMInstance();
  int p = rpm->isEngineRunning() ? DEFAULT_INJECTION_PRESSURE : 0;
  txData[3] = MSB(p);
  txData[4] = LSB(p);
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
  int intake_Pressure = (getGlobalValue(F_PRESSURE) * 255.0f / 2.55f);
  if(intake_Pressure > 255) {
    intake_Pressure = 255;
  }
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
  int volt = int(getGlobalValue(F_VOLTS) * 1024);
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

  unsupported(responseId, mode, pid);
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

  unsupported(responseId, mode, pid);
  return true;
}

static bool handleMode09(uint8_t pid, uint32_t responseId, uint8_t mode, uint8_t *txData, bool *tx) {
  if(pid == 0x00){        // Supported PIDs 01-20
    txData[0] = 0x06;

    txData[3] = 0x54;
    txData[4] = 0xC8;
    txData[5] = 0x00;
    txData[6] = 0x00;
    *tx = true;
  }
  else if(pid == REQUEST_VIN){    // VIN (17 to 20 Bytes) Uses ISO-TP
    uint8_t VIN[] = {(uint8_t)(0x40 | mode), pid, 0x01, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD};
    for(int a = 0; a < (int)strlen(vehicle_Vin); a++) {
      VIN[a + 3] = (uint8_t)vehicle_Vin[a];
    }
    iso_tp(responseId, 20, VIN);
  }
  else if(pid == 0x04){    // Calibration ID (Ford part number format, e.g. XS4A-12A650-AXB)
    // Mode 09 PID 04: fixed 16-byte CALID, null padded.
    uint8_t CID[3 + 16] = {(uint8_t)(0x40 | mode), pid, 0x01};
    int calLen = (int)strlen(ecu_CalibrationId);
    if(calLen > 16) calLen = 16;
    memcpy(&CID[3], ecu_CalibrationId, (size_t)calLen);
    iso_tp(responseId, (int)sizeof(CID), CID);
  }
  else if(pid == 0x06){    // CVN
    uint8_t CVN[] = {(uint8_t)(0x40 | mode), pid, 0x02, 0x11, 0x42, 0x42, 0x42, 0x22, 0x43, 0x43, 0x43};
    iso_tp(responseId, 11, CVN);
  }
  else if(pid == 0x09){    // ECU name message count for PID 0A.
    txData[0] = 0x03;
    txData[3] = 0x01;
    *tx = true;
  }
  else if(pid == 0x0A){    // ECM Name
    // Mode 09 PID 0A: fixed 20-byte ECU name, null padded.
    uint8_t ECMname[3 + 20] = {(uint8_t)(0x40 | mode), pid, 0x01};
    int nameLen = (int)strlen(ecu_Name);
    if(nameLen > 20) nameLen = 20;
    memcpy(&ECMname[3], ecu_Name, (size_t)nameLen);
    iso_tp(responseId, (int)sizeof(ECMname), ECMname);
  }
  else if(pid == 0x0D){    // ESN
    uint8_t ESN[] = {(uint8_t)(0x40 | mode), pid, 0x01, 0x41, 0x72, 0x64, 0x75, 0x69, 0x6E, 0x6F, 0x2D, 0x4F, 0x42, 0x44, 0x49, 0x49, 0x73, 0x69, 0x6D, 0x00};
    iso_tp(responseId, 20, ESN);
  }
  else{
    unsupported(responseId, mode, pid);
  }

  return true;
}

static bool handleObdService(uint8_t mode, uint8_t pid, uint32_t responseId, uint8_t *txData, bool *tx) {
  if(mode == L1) {
    return handleMode01(pid, responseId, mode, txData, tx);
  }

  if(mode == L2 || mode == L5 || mode == L8) {
    unsupported(responseId, mode, pid);
    return true;
  }

  if(mode == L3) {
    uint8_t DTCs[24] = {0};
    int dtcLen = fillDtcPayload((uint8_t)(0x40 | mode), DTC_KIND_STORED, DTCs, sizeof(DTCs));
    iso_tp(responseId, dtcLen, DTCs);
    return true;
  }

  if(mode == L4) {
    dtcManagerClearAll();
    txData[0] = 0x01;
    *tx = true;
    return true;
  }

  if(mode == L6) {
    return handleMode06(pid, responseId, mode, txData, tx);
  }

  if(mode == L7) {
    uint8_t DTCs[24] = {0};
    int dtcLen = fillDtcPayload((uint8_t)(0x40 | mode), DTC_KIND_PENDING, DTCs, sizeof(DTCs));
    iso_tp(responseId, dtcLen, DTCs);
    return true;
  }

  if(mode == L9) {
    return handleMode09(pid, responseId, mode, txData, tx);
  }

  if(mode == L10) {
    uint8_t DTCs[24] = {0};
    int dtcLen = fillDtcPayload((uint8_t)(0x40 | mode), DTC_KIND_PERMANENT, DTCs, sizeof(DTCs));
    iso_tp(responseId, dtcLen, DTCs);
    return true;
  }

  return false;
}

// Pack a string into a fixed-width null-padded field at buf[0..width-1].
static void packField(uint8_t *buf, const char *str, int width) {
  int len = (int)strlen(str);
  if(len > width) len = width;
  memcpy(buf, str, (size_t)len);
  for(int i = len; i < width; i++) buf[i] = 0x00;
}

// Build and send a UDS 0x22 positive response with a fixed-width ASCII field.
static void send22Field(uint32_t responseId, uint16_t did, const char *str, int width) {
  if(width < 0) width = 0;
  if(width > 40) width = 40;

  uint8_t payload[3 + 40] = {0x62, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF)};
  packField(&payload[3], str, width);
  iso_tp(responseId, 3 + width, payload);
}

// Build and send a UDS 0x22 response with 32-bit big-endian value.
static void send22U32(uint32_t responseId, uint16_t did, uint32_t value) {
  uint8_t payload[7] = {
    0x62, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF),
    (uint8_t)((value >> 24) & 0xFF),
    (uint8_t)((value >> 16) & 0xFF),
    (uint8_t)((value >> 8) & 0xFF),
    (uint8_t)(value & 0xFF)
  };
  iso_tp(responseId, (int)sizeof(payload), payload);
}

static bool handleUdsService(uint8_t mode, uint8_t numofBytes, uint8_t *data, uint32_t responseId, uint8_t *txData, bool *tx) {
  if(mode == 0x10) {
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }

    uint8_t subFunction = data[2] & 0x7F;
    if(subFunction == 0x01 || subFunction == 0x02 || subFunction == 0x03) {
      udsSession = subFunction;
      uint8_t udsRsp[] = {0x06, 0x50, subFunction, 0x00, 0x32, 0x01, 0xF4, PAD};
      hal_can_send(obdCan, responseId, 8, udsRsp);
    } else {
      negAck(responseId, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    }
    return true;
  }

  if(mode == 0x11) {
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }

    uint8_t subFunction = data[2] & 0x7F;
    txData[0] = 0x02;
    txData[1] = 0x51;
    txData[2] = subFunction;
    *tx = true;
    return true;
  }

  if(mode == 0x14) {
    // Most testers send 0x14 + 3-byte groupOfDTC.
    if(!requireMinLength(responseId, mode, numofBytes, 4)) {
      return true;
    }

    dtcManagerClearAll();

    txData[0] = 0x01;
    txData[1] = 0x54;
    *tx = true;
    return true;
  }

  if(mode == 0x19) {
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
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
      payload[p++] = 0x59;
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
      txData[1] = 0x59;
      txData[2] = 0x0A;
      txData[3] = storedCount;
      *tx = true;
      return true;
    }

    negAck(responseId, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    return true;
  }

  if(mode == 0x22) {
    if(!requireMinLength(responseId, mode, numofBytes, 3)) {
      return true;
    }

    uint16_t did = (uint16_t(data[2]) << 8) | uint16_t(data[3]);
    deb("UDS 0x22 DID=0x%04X", did);
    if(did == 0xF190) {
      uint8_t payload[] = {
        0x62, 0xF1, 0x90,
        PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD,
        PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD
      };
      for(int a = 0; a < 17 && a < (int)strlen(vehicle_Vin); a++) {
        payload[3 + a] = uint8_t(vehicle_Vin[a]);
      }
      iso_tp(responseId, 20, payload);
    } else if(did == 0xF186) {
      uint8_t udsRsp[] = {0x04, 0x62, 0xF1, 0x86, udsSession, PAD, PAD, PAD};
      hal_can_send(obdCan, responseId, 8, udsRsp);
    } else if(did == 0xF187) {
      // Spare part number
      uint8_t payload[3 + 16] = {0x62, 0xF1, 0x87};
      memcpy(&payload[3], ecu_PartNumber, strlen(ecu_PartNumber));
      iso_tp(responseId, 3 + (int)strlen(ecu_PartNumber), payload);
    } else if(did == 0xF188) {
      // Fordiag maps this DID as "SW version".
      send22Field(responseId, did, ecu_SwVersion, 4);
    } else if(did == 0xF189) {
      // ECU software version
      uint8_t payload[3 + 8] = {0x62, 0xF1, 0x89};
      memcpy(&payload[3], ecu_SwVersion, strlen(ecu_SwVersion));
      iso_tp(responseId, 3 + (int)strlen(ecu_SwVersion), payload);
    } else if(did == 0xF18A) {
      // System supplier identifier
      uint8_t payload[] = {0x62, 0xF1, 0x8A, 'F', 'O', 'R', 'D', ' ', 'E', 'E', 'C', '-', 'V'};
      iso_tp(responseId, (int)sizeof(payload), payload);
    } else if(did == 0xF18B) {
      // ECU manufacture date
      uint8_t payload[3 + 8] = {0x62, 0xF1, 0x8B};
      memcpy(&payload[3], ecu_SwDate, strlen(ecu_SwDate));
      iso_tp(responseId, 3 + (int)strlen(ecu_SwDate), payload);
    } else if(did == 0xF18C) {
      // ECU serial number
      uint8_t payload[3 + 17] = {0x62, 0xF1, 0x8C};
      memcpy(&payload[3], vehicle_Vin, strlen(vehicle_Vin));
      iso_tp(responseId, 3 + (int)strlen(vehicle_Vin), payload);
    } else if(did == 0xF191) {
      // ECU hardware version number
      uint8_t payload[3 + 8] = {0x62, 0xF1, 0x91};
      memcpy(&payload[3], ecu_HardwareId, strlen(ecu_HardwareId));
      iso_tp(responseId, 3 + (int)strlen(ecu_HardwareId), payload);
    } else if(did == 0xF197) {
      // System name / engine type
      uint8_t payload[3 + 20] = {0x62, 0xF1, 0x97};
      const char *sysName = "FORD 1.8 TDDI VP37";
      memcpy(&payload[3], sysName, strlen(sysName));
      iso_tp(responseId, 3 + (int)strlen(sysName), payload);
    } else if(did == 0xF19E) {
      // ODX file identifier
      uint8_t payload[3 + 8] = {0x62, 0xF1, 0x9E};
      memcpy(&payload[3], ecu_Model, strlen(ecu_Model));
      iso_tp(responseId, 3 + (int)strlen(ecu_Model), payload);
    } else if(did == 0x0200) {
      // ECU type/capabilities (first DID ForDiag reads; diesel VP37 indicator)
      uint8_t payload[] = {0x62, 0x02, 0x00, 0x01, 0x04};
      iso_tp(responseId, (int)sizeof(payload), payload);
    } else if(did == 0xE6F3) {
      send22Field(responseId, did, ecu_Model, 8);
    } else if((did & 0xFF00) == 0xF400 &&
              did != 0xF400 && did != 0xF401 && did != 0xF402 && did != 0xF403 &&
              did != 0xF406 && did != 0xF408 && did != 0xF409) {
      // ForDiag live-data mirror: DID F4xx -> OBD mode 01 PID xx payload.
      uint8_t pid = (uint8_t)(did & 0x00FF);
      uint8_t dataBytes[4] = {0};
      int dataLen = 0;
      if(encodeMode01PidData(pid, dataBytes, &dataLen)) {
        uint8_t payload[3 + 4] = {0x62, (uint8_t)(did >> 8), (uint8_t)did};
        memcpy(&payload[3], dataBytes, (size_t)dataLen);
        iso_tp(responseId, 3 + dataLen, payload);
      } else {
        // Keep alive with a deterministic zero payload for unknown F4xx PIDs.
        uint8_t payload[] = {0x62, (uint8_t)(did >> 8), (uint8_t)did, 0x00};
        iso_tp(responseId, (int)sizeof(payload), payload);
      }
    } else if(did >= 0xF400 && did <= 0xF409) {
      // Ford EEC-V identification DIDs used by identification screen.
      const char *val;
      int width;
      switch(did) {
        case 0xF400: val = ecu_Model;         width = 8;  break; // Model
        case 0xF401: val = ecu_Type;          width = 8;  break; // Type
        case 0xF402: val = ecu_SubType;       width = 8;  break; // Subtype
        case 0xF403: val = ecu_CatchCode;     width = 8;  break; // Catch CODE
        case 0xF404: val = ecu_SwDate;        width = 8;  break; // SW date
        case 0xF405: val = ecu_CalibrationId; width = 16; break; // Calibr.ID
        case 0xF406: val = ecu_PartNumber;    width = 16; break; // Part number
        case 0xF407: val = ecu_HardwareId;    width = 8;  break; // Hardware
        case 0xF408: send22U32(responseId, did, 0x00080000u); return true; // ROM size
        case 0xF409: val = ecu_Copyright;     width = 16; break; // Copyright
        default:     val = ecu_SwVersion;     width = 4;  break;
      }
      send22Field(responseId, did, val, width);
    } else if(did == 0xF113) {
      // Fordiag maps this DID as "Part number".
      send22Field(responseId, did, ecu_PartNumber, 16);
    } else if(did == 0xF180) {
      send22Field(responseId, did, ecu_SwVersion, 4);
    } else if(did == 0xE300) {
      send22Field(responseId, did, ecu_Type, 8);
    } else if(did == 0xE301) {
      send22Field(responseId, did, ecu_SubType, 8);
    } else if(did == 0xE302) {
      send22Field(responseId, did, ecu_Copyright, 16);
    } else if(did == 0xE303) {
      send22Field(responseId, did, ecu_CatchCode, 8);
    } else if(did == 0xE200) {
      send22Field(responseId, did, ecu_SwDate, 8);
    } else if(did == 0xE217) {
      send22Field(responseId, did, ecu_CalibrationId, 16);
    } else if(did == 0xE21A) {
      send22Field(responseId, did, ecu_HardwareId, 8);
    } else if(did == 0xE219) {
      send22U32(responseId, did, 0x00080000u);
    } else if(did == 0xC92E) {
      send22Field(responseId, did, ecu_CatchCode, 8);
    } else if(did == 0xC900) {
      send22Field(responseId, did, ecu_PartNumber, 16);
    } else {
      negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
    }
    return true;
  }

  if(mode == 0x12) {
    // KWP2000 service 0x12 - ReadDataByLocalIdentifier
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }

    uint8_t localId = data[2];
    deb("KWP 0x12 localId=0x%02X", localId);

    // localId -> string pointer; NULL means use special handling below
    const char *str = NULL;
    switch(localId) {
      case 0x80: str = ecu_CalibrationId; break;
      case 0x81: str = ecu_SwDate;        break;
      case 0x82: str = ecu_PartNumber;    break;
      case 0x86: str = ecu_Model;         break;
      case 0x90: str = vehicle_Vin;       break;
      case 0x91: str = ecu_Model;         break;
      case 0x92: str = ecu_Type;          break;
      case 0x93: str = ecu_SubType;       break;
      case 0x94: str = ecu_CatchCode;     break;
      case 0x95: str = vehicle_Vin;       break;
      case 0x96: str = ecu_SwVersion;     break;
      case 0x97: str = ecu_SwDate;        break;
      case 0x98: str = ecu_CalibrationId; break;
      case 0x99: str = ecu_PartNumber;    break;
      case 0x9A: str = ecu_HardwareId;    break;
      case 0x9C: str = ecu_Copyright;     break;
      case 0x9B: {
        // ROM size: 512 KB = 0x00080000
        uint8_t rsp[] = {0x52, localId, 0x00, 0x08, 0x00, 0x00};
        iso_tp(responseId, (int)sizeof(rsp), rsp);
        return true;
      }

      // ---- ForDiag EEC-V identification blocks -------------------------
      case 0x33: {
        // Software calibration block used by Fordiag:
        // SwVersion(4) + SwDate(8) + CalibId(16)
        uint8_t resp[2 + 16 + 4 + 8];
        resp[0] = 0x52; resp[1] = localId;
        packField(&resp[2],  ecu_SwVersion,       4);
        packField(&resp[6],  ecu_SwDate,          8);
        packField(&resp[14], ecu_CalibrationId,  16);
        iso_tp(responseId, (int)sizeof(resp), resp);
        return true;
      }
      case 0xFE: {
        // Full ECU identification block (12 fields):
        // Model(8) + Type(8) + Subtype(8) + VIN(17) + CatchCode(8)
        // + SwVersion(4) + SwDate(8) + CalibId(16) + PartNumber(16)
        // + Hardware(8) + RomSize(8) + Copyright(16)
        // RomSize uses U32 big-endian in first 4 bytes, trailing 4 bytes are zero.
        uint8_t resp[2 + 8 + 8 + 8 + 17 + 8 + 4 + 8 + 16 + 16 + 8 + 8 + 16];
        resp[0] = 0x52; resp[1] = localId;
        int off = 2;
        packField(&resp[off], ecu_Model,       8);  off += 8;
        packField(&resp[off], ecu_Type,        8);  off += 8;
        packField(&resp[off], ecu_SubType,     8);  off += 8;
        packField(&resp[off], vehicle_Vin,    17);  off += 17;
        packField(&resp[off], ecu_CatchCode,   8);  off += 8;
        packField(&resp[off], ecu_SwVersion,   4);  off += 4;
        packField(&resp[off], ecu_SwDate,      8);  off += 8;
        packField(&resp[off], ecu_CalibrationId, 16); off += 16;
        packField(&resp[off], ecu_PartNumber,  16); off += 16;
        packField(&resp[off], ecu_HardwareId,   8); off += 8;
        resp[off++] = 0x00;
        resp[off++] = 0x08;
        resp[off++] = 0x00;
        resp[off++] = 0x00;
        resp[off++] = 0x00;
        resp[off++] = 0x00;
        resp[off++] = 0x00;
        resp[off++] = 0x00;
        packField(&resp[off], ecu_Copyright,   16); off += 16;
        iso_tp(responseId, off, resp);
        return true;
      }
      case 0xFF: {
        // Supported local identifiers list
        uint8_t resp[] = {0x52, localId, 0x33, 0xFE};
        iso_tp(responseId, (int)sizeof(resp), resp);
        return true;
      }
      // ------------------------------------------------------------------

      default:
        negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
        return true;
    }

    uint8_t payload[64];
    payload[0] = 0x52;
    payload[1] = localId;
    int len = 2;
    int strLen = (int)strlen(str);
    if(len + strLen > (int)sizeof(payload)) {
      strLen = (int)sizeof(payload) - len;
    }
    memcpy(&payload[len], str, (size_t)strLen);
    len += strLen;
    iso_tp(responseId, len, payload);
    return true;
  }

  if(mode == 0x3E) {
    if(!requireMinLength(responseId, mode, numofBytes, 2)) {
      return true;
    }

    uint8_t subFunction = data[2];
    if((subFunction & 0x7F) != 0x00) {
      negAck(responseId, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
      return true;
    }

    // 0x80 means suppress positive response.
    if((subFunction & 0x80) != 0) {
      return true;
    }

    txData[0] = 0x02;
    txData[1] = 0x7E;
    txData[2] = 0x00;
    *tx = true;
    return true;
  }

  return false;
}

void obdReq(uint32_t requestId, uint8_t *data){
  uint8_t numofBytes = data[0];
  // Ignore ISO-TP control/segmentation frames arriving as normal OBD requests.
  if(numofBytes > 8) {
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
  uint8_t txData[] = {0x00,(uint8_t)(0x40 | mode),pid,PAD,PAD,PAD,PAD,PAD};

  if(mode == L1 && pid <= PID_LAST) {
    deb("OBD-2 pid:0x%02x (%s) length:0x%02x mode:0x%02x",  pid, getPIDName(pid), numofBytes, mode);
  } else {
    deb("OBD/UDS service:0x%02x length:0x%02x", mode, numofBytes);
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
void unsupported(uint32_t responseId, uint8_t mode, uint8_t pid){
  negAck(responseId, mode, 0x12);
  unsupportedPrint(mode, pid);  
}


// Generic debug serial output
void negAck(uint32_t responseId, uint8_t mode, uint8_t reason){
  uint8_t txData[] = {0x03,0x7F,mode,reason,PAD,PAD,PAD,PAD};
  hal_can_send(obdCan, responseId, 8, txData);
}


// Generic debug serial output
void unsupportedPrint(uint8_t mode, uint8_t pid){
  char msgstring[64];
  snprintf(msgstring, sizeof(msgstring) - 1, "Mode $%02X: Unsupported PID $%02X requested!", mode, pid);
  deb(msgstring);
}


// Generic debug serial output for UDS/OBD service-level rejects.
void unsupportedServicePrint(uint8_t mode){
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
      derr("ISO-TP timeout waiting for FC");
      s_isoTp.state = ISO_TP_IDLE;
      return;
    }
    if(!hal_gpio_read(CAN1_INT)) {
      bool gotFrame = hal_can_receive(obdCan, &rxId, &dlc, rxBuf);
      bool idMatches = (rxId == s_isoTp.requestId) || (rxId == LISTEN_ID) || (rxId == FUNCTIONAL_ID);
      if(gotFrame && dlc >= 3 && idMatches && ((rxBuf[0] & 0xF0) == 0x30)) {
        uint8_t fcType = rxBuf[0] & 0x0F;
        if(fcType == 0x00) {
          s_isoTp.blockSize  = rxBuf[1];
          s_isoTp.stMin      = stMinToMs(rxBuf[2]);
          s_isoTp.blockSent  = 0;
          s_isoTp.lastCfTime = 0;
          s_isoTp.state      = ISO_TP_SEND_CF;
        } else if(fcType == 0x01) {
          s_isoTp.fcWaitStart = hal_millis(); // extend wait window
        } else if(fcType == 0x02) {
          derr("ISO-TP FC abort from tester");
          s_isoTp.state = ISO_TP_IDLE;
        }
      }
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
