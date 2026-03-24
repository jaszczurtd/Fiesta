
//based on Open-Ecu-Sim-OBD2-FW
//https://github.com/spoonieau/OBD2-ECU-Simulator
// and CAN OBD & UDS Simulator Written By: Cory J. Fowler  December 20th, 2016

#include "obd-2.h"
#include "dtcManager.h"

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

#define ISO_TP_MAX_PAYLOAD 64
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
  uint8_t        index;
  uint8_t        stMin;
  uint8_t        blockSize;
  uint8_t        blockSent;
  unsigned long  fcWaitStart;
  unsigned long  lastCfTime;
  iso_tp_state_t state;
} iso_tp_ctx_t;

static iso_tp_ctx_t s_isoTp = {.state = ISO_TP_IDLE};

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
    m_delay(SECOND);
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
  else if(pid == 0x04){    // Calibration ID
    uint8_t CID[] = {(uint8_t)(0x40 | mode), pid, 0x01, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD};
    storeECUName(CID, 3);
    iso_tp(responseId, 18, CID);
  }
  else if(pid == 0x06){    // CVN
    uint8_t CVN[] = {(uint8_t)(0x40 | mode), pid, 0x02, 0x11, 0x42, 0x42, 0x42, 0x22, 0x43, 0x43, 0x43};
    iso_tp(responseId, 11, CVN);
  }
  else if(pid == 0x09){    // ECU name message count for PID 0A.
    uint8_t ECUname[] = {(uint8_t)(0x40 | mode), pid, 0x01, 'E', 'C', 'U', 0x00, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD};
    storeECUName(ECUname, 7);
    iso_tp(responseId, 23, ECUname);
  }
  else if(pid == 0x0A){    // ECM Name
    uint8_t ECMname[] = {(uint8_t)(0x40 | mode), pid, 0x01, 'E', 'C', 'M', 0x00, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD, PAD};
    storeECUName(ECMname, 7);
    iso_tp(responseId, 23, ECMname);
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
    if(!requireMinLength(responseId, mode, numofBytes, 4)) {
      return true;
    }

    uint16_t did = (uint16_t(data[2]) << 8) | uint16_t(data[3]);
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
      uint8_t payload[] = {0x62, 0xF1, 0x87, 'F', 'O', 'R', 'D', '_', 'P', 'C', 'M'};
      iso_tp(responseId, 11, payload);
    } else if(did == 0xF18C) {
      uint8_t payload[] = {0x62, 0xF1, 0x8C, 'F', 'I', 'E', 'S', 'T', 'A', '_', 'E', 'C', 'U'};
      iso_tp(responseId, 12, payload);
    } else {
      negAck(responseId, mode, NRC_REQUEST_OUT_OF_RANGE);
    }
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
  if(numofBytes < 1) {
    return;
  }

  uint32_t responseId = REPLY_ID;
  if(requestId == LISTEN_ID) {
    responseId = REPLY_ID;
  }

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
      if(gotFrame && dlc >= 3 && (rxId == LISTEN_ID) && ((rxBuf[0] & 0xF0) == 0x30)) {
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

