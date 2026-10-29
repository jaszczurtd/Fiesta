#include "can.h"

#include "dtcManager.h"
#include "rtcTime.h"
#include <utils/multicoreWatchdog.h>
#include <utils/tools_common_defs.h>

/**
 * @brief Mark that a CAN interrupt signalled pending traffic.
 */
void receivedCanMessage(void);

typedef struct {
  uint8_t frameNumberVal;
  bool interruptPending;
  bool dpfConnectedFlag;
  bool dpfEverSeenFlag;
  unsigned long dpfMessagesCount;
  unsigned long lastDpfMessagesCount;
  bool egtConnectedFlag;
  bool egtEverSeenFlag;
  unsigned long egtMessagesCount;
  unsigned long lastEgtMessagesCount;
  bool egtValidFlag; /* valid bits of the last EGT frame */
  bool dpfTempValidFlag;
  bool oilConnectedFlag;
  bool oilValidFlag; /* valid bit of the last oil and speed frame */
  unsigned long oilMessagesCount;
  unsigned long lastOilMessagesCount;
  hal_can_t canBusHandle;
  bool isInitialized;
  bool rpmHasBeenSent;
  int32_t lastRpmSent;
  uint32_t lastRpmSentAtMs;
  uint32_t lastRpmAttemptAtMs;
  bool rpmRetryPending;
  float lastTurboHiSent;
  float lastTurboLoSent;
  float lastTurboHiDesiredSent;
  float lastTurboLoDesiredSent;
  int32_t lastThrottleSent;
  uint16_t txFailures;  // failed sends since the last connection check
  uint8_t txFailDetail; // DTC detail of the last failed send
} can_state_t;

static can_state_t s_canState = {.frameNumberVal = 0u,
                                 .interruptPending = false,
                                 .dpfConnectedFlag = false,
                                 .dpfEverSeenFlag = false,
                                 .dpfMessagesCount = 0uL,
                                 .lastDpfMessagesCount = 0uL,
                                 .egtConnectedFlag = false,
                                 .egtEverSeenFlag = false,
                                 .egtMessagesCount = 0uL,
                                 .lastEgtMessagesCount = 0uL,
                                 .egtValidFlag = false,
                                 .dpfTempValidFlag = false,
                                 .oilConnectedFlag = false,
                                 .oilValidFlag = false,
                                 .oilMessagesCount = 0uL,
                                 .lastOilMessagesCount = 0uL,
                                 .canBusHandle = NULL,
                                 .isInitialized = false,
                                 .rpmHasBeenSent = false,
                                 .lastRpmSent = 0,
                                 .lastRpmSentAtMs = 0u,
                                 .lastRpmAttemptAtMs = 0u,
                                 .rpmRetryPending = false,
                                 .lastTurboHiSent = (float)C_INIT_VAL,
                                 .lastTurboLoSent = (float)C_INIT_VAL,
                                 .lastTurboHiDesiredSent = (float)C_INIT_VAL,
                                 .lastTurboLoDesiredSent = (float)C_INIT_VAL,
                                 .lastThrottleSent = (int32_t)C_INIT_VAL,
                                 .txFailures = 0u,
                                 .txFailDetail = DTC_DETAIL_NONE};

/**
 * @brief Map a transmitted frame id to the DTC detail naming it.
 * @param id CAN identifier of the frame.
 * @return DTC_DETAIL_CAN_TX_* value for that frame.
 */
static uint8_t canTxDetailForId(uint32_t id) {
  uint8_t detail = DTC_DETAIL_CAN_TX_OTHER;
  switch (id) {
  case CAN_ID_RPM:
    detail = DTC_DETAIL_CAN_TX_RPM;
    break;
  case CAN_ID_ECU_UPDATE_01:
    detail = DTC_DETAIL_CAN_TX_ECU_UPDATE_01;
    break;
  case CAN_ID_ECU_UPDATE_02:
    detail = DTC_DETAIL_CAN_TX_ECU_UPDATE_02;
    break;
  case CAN_ID_ECU_UPDATE_03:
    detail = DTC_DETAIL_CAN_TX_ECU_UPDATE_03;
    break;
  case CAN_ID_TURBO_PRESSURE:
    detail = DTC_DETAIL_CAN_TX_TURBO;
    break;
  case CAN_ID_THROTTLE:
    detail = DTC_DETAIL_CAN_TX_THROTTLE;
    break;
  default:
    break;
  }
  return detail;
}

/**
 * @brief Send one frame on the ECU bus and account for a failed transmit.
 * @param id CAN identifier.
 * @param len Payload length.
 * @param buf Payload bytes.
 * @return HAL_OK when the frame went out, otherwise the hal_can_send() error
 *         (the HAL logs it with the frame id).
 */
static hal_status_t canSend(uint32_t id, uint8_t len, const uint8_t *buf) {
  const hal_status_t sent = hal_can_send(s_canState.canBusHandle, id, len, buf);
  if (sent != HAL_OK) {
    if (s_canState.txFailures < UINT16_MAX) {
      s_canState.txFailures++;
    }
    s_canState.txFailDetail = canTxDetailForId(id);
  }
  return sent;
}

/**
 * @brief Reset the RPM publisher cache and retry state.
 */
static void resetRpmPublisher(void) {
  const uint32_t now = hal_millis();
  s_canState.rpmHasBeenSent = false;
  s_canState.lastRpmSent = 0;
  s_canState.lastRpmSentAtMs = now;
  s_canState.lastRpmAttemptAtMs = now;
  s_canState.rpmRetryPending = false;
}

#ifdef UNIT_TEST
hal_can_t canTestGetCanHandle(void) { return s_canState.canBusHandle; }

void canTestResetRpmPublisher(void) { resetRpmPublisher(); }
#endif

static hal_can_config_t can0Config(void) {
  hal_can_config_t cfg = JH_HW_NODE_CAN_MAIN_CONFIG_INIT;
  cfg.mcp2515.one_shot_tx = true;
  return cfg;
}

hal_status_t canInit(int retries) {
  s_canState.dpfConnectedFlag = false;
  s_canState.dpfEverSeenFlag = false;
  s_canState.dpfMessagesCount = 0uL;
  s_canState.lastDpfMessagesCount = 0uL;
  s_canState.egtConnectedFlag = false;
  s_canState.egtEverSeenFlag = false;
  s_canState.egtMessagesCount = 0uL;
  s_canState.lastEgtMessagesCount = 0uL;
  s_canState.egtValidFlag = false;
  s_canState.dpfTempValidFlag = false;
  s_canState.oilConnectedFlag = false;
  s_canState.oilValidFlag = false;
  s_canState.oilMessagesCount = 0uL;
  s_canState.lastOilMessagesCount = 0uL;
  resetRpmPublisher();

  hal_can_config_t canCfg = can0Config();
  const hal_status_t st = hal_can_create_with_retry(
      &canCfg, JH_HW_NODE_CAN_MAIN_PIN_INT, receivedCanMessage,
      retries > 0 ? retries - 1 : 0, watchdog_feed, &s_canState.canBusHandle);
  s_canState.isInitialized = (st == HAL_OK);

  if (s_canState.isInitialized) {
    deb("CAN BUS Shield init ok!");
  } else {
    derr("CAN BUS Shield init problem (%s). CAN communication would not be "
         "possible.",
         hal_status_to_string(st));
  }
  return st;
}

void CAN_sendAll(void) {
  CAN_updaterecipients_01();
  hal_delay_ms(CORE_OPERATION_DELAY);
  CAN_updaterecipients_02();
  hal_delay_ms(CORE_OPERATION_DELAY);
  CAN_sendThrottleUpdate();
  hal_delay_ms(CORE_OPERATION_DELAY);
  CAN_sendTurboUpdate();
}

void CAN_updaterecipients_01(void) {

  if (s_canState.isInitialized) {
    int hi, lo;

    uint8_t buf[CAN_FRAME_MAX_LENGTH] = {0};
    buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;

    buf[CAN_FRAME_ECU_UPDATE_ENGINE_LOAD] =
        (uint8_t)getGlobalValue(F_CALCULATED_ENGINE_LOAD);

    fiesta_can_split_decimal_tenths(getGlobalValue(F_VOLTS), &hi, &lo);
    buf[CAN_FRAME_ECU_UPDATE_VOLTS_HI] = (uint8_t)hi;
    buf[CAN_FRAME_ECU_UPDATE_VOLTS_LO] = (uint8_t)lo;

    buf[CAN_FRAME_ECU_UPDATE_COOLANT] =
        hal_can_encode_temp_i8(getGlobalValue(F_COOLANT_TEMP));
    buf[CAN_FRAME_ECU_UPDATE_OIL] =
        hal_can_encode_temp_i8(getGlobalValue(F_OIL_TEMP));

    (void)canSend(CAN_ID_ECU_UPDATE_01, CAN_FRAME_MAX_LENGTH, buf);

    (void)memset(buf, 0, sizeof(buf));
    buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;
    buf[CAN_FRAME_ECU_UPDATE_INTAKE] =
        hal_can_encode_temp_i8(getGlobalValue(F_INTAKE_TEMP));

    int16_t fuel = (int16_t)getGlobalValue(F_FUEL);
    buf[CAN_FRAME_ECU_UPDATE_FUEL_HI] = MSB(fuel);
    buf[CAN_FRAME_ECU_UPDATE_FUEL_LO] = LSB(fuel);

    (void)canSend(CAN_ID_ECU_UPDATE_02, CAN_FRAME_MAX_LENGTH, buf);

    buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;
    buf[CAN_FRAME_ECU_UPDATE_PRESSURE_PERCENTAGE] =
        getGlobalValue(F_PRESSURE_PERCENTAGE);
    buf[CAN_FRAME_ECU_UPDATE_FUEL_TEMP] =
        hal_can_encode_temp_i8(getGlobalValue(F_FUEL_TEMP));
    buf[CAN_FRAME_ECU_UPDATE_FAN_ENABLED] = getGlobalValue(F_FAN_ENABLED);

    (void)canSend(CAN_ID_ECU_UPDATE_03, CAN_FRAME_MAX_LENGTH, buf);
  }
}

void CAN_updaterecipients_02(void) {
  if (s_canState.isInitialized) {
    const uint32_t now = hal_millis();
    const int32_t rpm = RPM_getCurrentRPM(getRPMInstance());
    const bool retryDue = s_canState.rpmRetryPending &&
                          ((uint32_t)(now - s_canState.lastRpmAttemptAtMs) >=
                           CAN_RPM_RETRY_INTERVAL_MS);
    const bool heartbeatDue = ((uint32_t)(now - s_canState.lastRpmSentAtMs) >=
                               CAN_RPM_HEARTBEAT_INTERVAL_MS);
    const bool valueChanged =
        (!s_canState.rpmHasBeenSent) || (s_canState.lastRpmSent != rpm);
    const bool shouldSend =
        s_canState.rpmRetryPending ? retryDue : (valueChanged || heartbeatDue);

    if (shouldSend) {
      uint8_t buf[CAN_FRAME_MAX_LENGTH] = {0};
      buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal;
      s_canState.frameNumberVal++;
      buf[CAN_FRAME_RPM_UPDATE_HI] = MSB(rpm);
      buf[CAN_FRAME_RPM_UPDATE_LO] = LSB(rpm);

      s_canState.lastRpmAttemptAtMs = now;
      const hal_status_t rpmSent =
          canSend(CAN_ID_RPM, CAN_FRAME_MAX_LENGTH, buf);
      if (rpmSent == HAL_OK) {
        s_canState.rpmHasBeenSent = true;
        s_canState.lastRpmSent = rpm;
        s_canState.lastRpmSentAtMs = now;
        s_canState.rpmRetryPending = false;
      } else {
        s_canState.rpmRetryPending = true;
      }
    }
  }
}

void CAN_sendTurboUpdate(void) {
  if (s_canState.isInitialized) {
    int hi, lo;
    int hi_d, lo_d;

    fiesta_can_split_decimal_tenths(getGlobalValue(F_PRESSURE), &hi, &lo);
    fiesta_can_split_decimal_tenths(getGlobalValue(F_PRESSURE_DESIRED), &hi_d,
                                    &lo_d);
    uint8_t buf[CAN_FRAME_MAX_LENGTH] = {0};

    s_canState.lastTurboLoSent = lo;
    s_canState.lastTurboHiSent = hi;

    s_canState.lastTurboLoDesiredSent = lo_d;
    s_canState.lastTurboHiDesiredSent = hi_d;

    buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;
    buf[CAN_FRAME_ECU_UPDATE_PRESSURE_HI] = (uint8_t)hi;
    buf[CAN_FRAME_ECU_UPDATE_PRESSURE_LO] = (uint8_t)lo;
    buf[CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_HI] = (uint8_t)hi_d;
    buf[CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_LO] = (uint8_t)lo_d;

    (void)canSend(CAN_ID_TURBO_PRESSURE, sizeof(buf), buf);
  }
}

void CAN_sendThrottleUpdate(void) {
  if (s_canState.isInitialized) {
    int32_t throttle = (int32_t)(getGlobalValue(F_THROTTLE_POS));
    s_canState.lastThrottleSent = throttle;

    uint8_t buf[CAN_FRAME_MAX_LENGTH] = {0};
    buf[CAN_FRAME_NUMBER] = s_canState.frameNumberVal++;
    buf[CAN_FRAME_THROTTLE_UPDATE_HI] = MSB(throttle);
    buf[CAN_FRAME_THROTTLE_UPDATE_LO] = LSB(throttle);

    (void)canSend(CAN_ID_THROTTLE, sizeof(buf), buf);
  }
}

void receivedCanMessage(void) { s_canState.interruptPending = true; }

/**
 * @brief Decode one received CAN frame and update project state.
 * @param canID CAN identifier of the received frame.
 * @param len Payload length in bytes.
 * @param buf Pointer to the received payload buffer.
 */
static void onCanFrame(uint32_t canID, uint8_t len, const uint8_t *buf) {
  s_canState.interruptPending = false;

  if (buf == NULL || len > CAN_FRAME_MAX_LENGTH) {
    derr("Received invalid CAN frame with ID: %03x, len: %d\n", canID, len);
    return;
  }

  switch (canID) {
  case CAN_ID_DPF: {
    if (len <= CAN_FRAME_DPF_UPDATE_DPF_REGEN) {
      derr("Received truncated DPF frame with len: %d", len);
      return;
    }
    s_canState.dpfMessagesCount++;
    s_canState.dpfEverSeenFlag = true;
    setGlobalValue(F_DPF_REGEN, buf[CAN_FRAME_DPF_UPDATE_DPF_REGEN]);
  } break;

  case CAN_ID_EGT_UPDATE: {
    if (len <= CAN_FRAME_EGT_UPDATE_STATUS) {
      derr("Received truncated EGT frame with len: %d", len);
      return;
    }
    s_canState.egtMessagesCount++;
    s_canState.egtEverSeenFlag = true;

    /* Signed whole degrees; a channel without its valid bit has no
     * measurement, which the frame carries as 0. */
    const uint8_t status = buf[CAN_FRAME_EGT_UPDATE_STATUS];
    s_canState.egtValidFlag = (status & CAN_EGT_STATUS_EGT_VALID) != 0u;
    s_canState.dpfTempValidFlag =
        (status & CAN_EGT_STATUS_DPF_TEMP_VALID) != 0u;
    float egt = 0.0f;
    float dpfTemp = 0.0f;
    if (s_canState.egtValidFlag) {
      egt = (float)fiesta_can_get_temp_i16(&buf[CAN_FRAME_EGT_UPDATE_EGT_HI]);
    }
    if (s_canState.dpfTempValidFlag) {
      dpfTemp = (float)fiesta_can_get_temp_i16(
          &buf[CAN_FRAME_EGT_UPDATE_DPF_TEMP_HI]);
    }
    setGlobalValue(F_EGT, egt);
    setGlobalValue(F_DPF_TEMP, dpfTemp);
  } break;

  case CAN_ID_CLOCK_BRIGHTNESS: {
    if (len <= CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_LO) {
      derr("Received truncated clock-brightness frame with len: %d", len);
      return;
    }
    setGlobalValue(
        F_CLOCK_BRIGHTNESS,
        jh_u16_from_bytes(buf[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_HI],
                          buf[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_LO]));
  } break;

  case CAN_ID_LUMENS: {
    if (len <= CAN_FRAME_LIGHTS_UPDATE_LO) {
      derr("Received truncated lumens frame with len: %d", len);
      return;
    }
    setGlobalValue(F_OUTSIDE_LUMENS, fiesta_can_join_decimal_tenths(
                                         buf[CAN_FRAME_LIGHTS_UPDATE_HI],
                                         buf[CAN_FRAME_LIGHTS_UPDATE_LO]));
  } break;

  case CAN_ID_OIL_AND_SPEED_MODULE_UPDATE: {
    if (len <= CAN_FRAME_OIL_AND_SPEED_STATUS) {
      derr("Received truncated oil and speed frame with len: %d", len);
      return;
    }
    /* A faulty oil pressure sender comes without its valid bit. */
    s_canState.oilMessagesCount++;
    s_canState.oilValidFlag = (buf[CAN_FRAME_OIL_AND_SPEED_STATUS] &
                               CAN_OIL_AND_SPEED_STATUS_OIL_VALID) != 0u;
    float oilPressure = 0.0f;
    if (s_canState.oilValidFlag) {
      oilPressure = fiesta_can_join_decimal_tenths(
          buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI],
          buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO]);
    }
    setGlobalValue(F_OIL_PRESSURE, oilPressure);
    setGlobalValue(F_ABS_CAR_SPEED, buf[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED]);
  } break;

  case CAN_ID_RTC_UPDATE: {
    if (len <= CAN_FRAME_RTC_UPDATE_INTEGRITY) {
      derr("Received truncated RTC frame with len: %d", len);
      return;
    }
    /* Without integrity the clock has no trusted UTC: forget the last time
     * instead of letting it age. */
    uint32_t utc = 0u;
    if (buf[CAN_FRAME_RTC_UPDATE_INTEGRITY] == 1u) {
      (void)fiesta_can_get_utc_datetime(&buf[CAN_FRAME_RTC_UPDATE_DT_HI],
                                        buf[CAN_FRAME_RTC_UPDATE_SECOND], &utc);
    }
    rtcTimeStore(utc);
  } break;

  case CAN_ID_GPS_EXT_LAT:
  case CAN_ID_GPS_EXT_LON_TIME:
  case CAN_ID_GPS_UTC_UPDATE:
  case CAN_ID_AIR_TEMP_UPDATE:
    /* Frames of Clocks and Fiesta_clock that the ECU does not use. */
    break;

  default:
    derr_limited("can", "received unknown CAN frame:%03x len:%d", canID, len);
    break;
  }
}

void canMainLoop(void) {
  if (s_canState.isInitialized) {
    const hal_status_t st =
        hal_can_process_all(s_canState.canBusHandle, onCanFrame, NULL);
    if (st != HAL_OK) {
      derr_limited("can", "CAN0 receive failed: %s", hal_status_to_string(st));
    }
  }
}

bool isDPFConnected(void) { return s_canState.dpfConnectedFlag; }

bool isEGTConnected(void) { return s_canState.egtConnectedFlag; }

bool isEGTValid(void) {
  return s_canState.egtConnectedFlag && s_canState.egtValidFlag;
}

bool isDPFTempValid(void) {
  return s_canState.egtConnectedFlag && s_canState.dpfTempValidFlag;
}

bool isOilPressureValid(void) {
  return s_canState.oilConnectedFlag && s_canState.oilValidFlag;
}

/**
 * @brief Raise or clear U0073 from the controller state and failed sends.
 * @note Bus-off from the controller wins; otherwise any send that the driver
 * refused since the last check names the frame in the DTC detail. One clean
 * check period clears the active flag again.
 */
static void canReportBusFault(void) {
  hal_can_state_t state = HAL_CAN_STATE_ERROR_ACTIVE;
  hal_status_t stateStatus = HAL_EUNINIT;
  if (s_canState.isInitialized) {
    stateStatus = hal_can_get_state(s_canState.canBusHandle, &state);
    if (stateStatus != HAL_OK) {
      derr_limited("can", "CAN0 state unreadable: %s",
                   hal_status_to_string(stateStatus));
    }
  }
  if ((stateStatus == HAL_OK) && (state == HAL_CAN_STATE_BUS_OFF)) {
    dtcManagerSetActiveDetail(DTC_CAN_BUS_FAULT, true, DTC_DETAIL_CAN_BUS_OFF);
  } else if (s_canState.txFailures > 0u) {
    dtcManagerSetActiveDetail(DTC_CAN_BUS_FAULT, true, s_canState.txFailDetail);
  } else {
    dtcManagerSetActive(DTC_CAN_BUS_FAULT, false);
  }
  s_canState.txFailures = 0u;
}

void canCheckConnection(void) {
  s_canState.lastThrottleSent = C_INIT_VAL;
  canReportBusFault();
  rtcTimeExpire();

  s_canState.egtConnectedFlag =
      (s_canState.egtMessagesCount != s_canState.lastEgtMessagesCount);
  s_canState.lastEgtMessagesCount = s_canState.egtMessagesCount;
  dtcManagerSetActive(DTC_EGT_COMM_LOST, s_canState.egtEverSeenFlag &&
                                             !s_canState.egtConnectedFlag);

  s_canState.oilConnectedFlag =
      (s_canState.oilMessagesCount != s_canState.lastOilMessagesCount);
  s_canState.lastOilMessagesCount = s_canState.oilMessagesCount;

  s_canState.dpfConnectedFlag =
      (s_canState.dpfMessagesCount != s_canState.lastDpfMessagesCount);
  s_canState.lastDpfMessagesCount = s_canState.dpfMessagesCount;
  dtcManagerSetActive(DTC_DPF_COMM_LOST, s_canState.dpfEverSeenFlag &&
                                             !s_canState.dpfConnectedFlag);
}
