#include "can.h"
#include "peripherials.h"
#include "timingDiag.h"

// State of core 0: decoded ECU frames and the ABS speed. Core 1 reads it only
// through the published snapshot.
static float canValues[F_LAST];

#ifdef UNIT_TEST
hal_can_t clocksTestGetCanHandle(void);
float *clocksTestCanValues(void);
#endif

static unsigned char frameNumber = 0;
static unsigned long ecuMessages = 0, lastEcuMessages = 0;
static bool ecuConnected = false;
static bool lastConnected = false;
static unsigned long dpfMessages = 0, lastDPFMessages = 0;
static bool dpfConnected = false;

static volatile bool interrupt = false;
static hal_can_t canHandle = NULL;

static uint32_t lastEcuFrameMs = 0u;
static uint32_t redrawSeq = 0u;
static bool stateChanged = false;
static bool absReady = false;

// hal_millis() of the last decode of each value; a value never decoded is not
// fresh.
static uint32_t valueUpdatedMs[F_LAST];
static bool valueDecoded[F_LAST];

// Store a decoded or measured value with its time.
static void putValue(int idx, float value) {
  canValues[idx] = value;
  valueUpdatedMs[idx] = hal_millis();
  valueDecoded[idx] = true;
}

// One more frame from the ECU: it is connected and fresh.
static void countEcuFrame(void) {
  ecuMessages++;
  ecuConnected = true;
  lastEcuFrameMs = hal_millis();
}

// A value the display draws at once has changed.
static void requestRedraw(void) {
  redrawSeq++;
  stateChanged = true;
}

static void publishState(void) {
  if (!stateChanged) {
    return;
  }
  stateChanged = false;

  static clocks_ecu_snapshot_t snapshot;
  memcpy(snapshot.values, canValues, sizeof(snapshot.values));
  snapshot.absReady = absReady;
  snapshot.ecuConnected = ecuConnected;
  snapshot.dpfConnected = dpfConnected;
  snapshot.ecuUpdatedMs = lastEcuFrameMs;
  snapshot.redrawSeq = redrawSeq;
  clocksStatePublishEcu(&snapshot);
}

static hal_status_t configureFiestaCanFilters(void) {
  const hal_can_filter_t filter = {CAN_FIESTA_STD_ID_BASE,
                                   CAN_FIESTA_STD_ID_MASK, 0u};

  hal_status_t st = HAL_OK;
  for (uint8_t index = 0u; index < HAL_CAN_MAX_FILTERS && st == HAL_OK;
       index++) {
    st = hal_can_set_filter(canHandle, index, &filter);
  }
  return st;
}

#ifdef UNIT_TEST
hal_can_t clocksTestGetCanHandle(void) { return canHandle; }
float *clocksTestCanValues(void) { return canValues; }
#endif

hal_status_t canInit(void) {
  ecuConnected = false;
  ecuMessages = lastEcuMessages = 0;
  dpfMessages = lastDPFMessages = 0;
  interrupt = false;
  memset(valueDecoded, 0, sizeof(valueDecoded));

  hal_can_config_t canCfg = JH_HW_NODE_CAN_CONFIG_INIT;

  hal_status_t st = hal_can_create_with_retry(&canCfg, JH_HW_NODE_CAN_PIN_INT,
                                              receivedCanMessage, MAX_RETRIES,
                                              hal_watchdog_feed, &canHandle);
  if (st != HAL_OK) {
    derr("CAN BUS Shield init failed: %s", hal_status_to_string(st));
    return st;
  }
  st = configureFiestaCanFilters();
  if (st != HAL_OK) {
    derr("CAN acceptance-filter configuration failed: %s",
         hal_status_to_string(st));
    hal_can_destroy(canHandle);
    canHandle = NULL;
    return st;
  }
  deb("CAN BUS Shield init ok!");
  canMainLoop();
  return HAL_OK;
}

/**
 * @brief Send one frame of Clocks; a failure is logged with its reason.
 * @param id CAN identifier.
 * @param buf CAN_FRAME_MAX_LENGTH payload bytes; byte 0 gets the frame number.
 */
static void canBroadcast(uint32_t id, uint8_t *buf) {
  buf[CAN_FRAME_NUMBER] = frameNumber++;
  const uint32_t sendStart = timingDiagLoopStart();
  const hal_status_t st =
      hal_can_send(canHandle, id, CAN_FRAME_MAX_LENGTH, buf);
  timingDiagCanSent(sendStart);
  if (st != HAL_OK) {
    derr_limited("can", "frame 0x%03lx not sent: %s", (unsigned long)id,
                 hal_status_to_string(st));
  }
}

void updateCANrecipients(void) {
  if (canHandle == NULL) {
    return; /* canInit() failed and said why */
  }
  uint8_t out[CAN_FRAME_MAX_LENGTH] = {};

  unsigned short br = (unsigned short)getLcdBrightness();
  out[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_HI] = MSB(br);
  out[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_LO] = LSB(br);
  canBroadcast(CAN_ID_CLOCK_BRIGHTNESS, out);

  clocks_local_snapshot_t local;
  clocksStateCopyLocal(&local);

  uint8_t oilAndSpeed[CAN_FRAME_MAX_LENGTH] = {};
  int hi = 0, lo = 0;
  if (local.oilValid) {
    fiesta_can_split_decimal_tenths(local.oilBar, &hi, &lo);
    oilAndSpeed[CAN_FRAME_OIL_AND_SPEED_STATUS] =
        CAN_OIL_AND_SPEED_STATUS_OIL_VALID;
  }
  oilAndSpeed[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI] = (uint8_t)hi;
  oilAndSpeed[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO] = (uint8_t)lo;
  oilAndSpeed[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED] =
      (uint8_t)canValues[F_ABS_CAR_SPEED];
  canBroadcast(CAN_ID_OIL_AND_SPEED_MODULE_UPDATE, oilAndSpeed);
}

void updateEGTrecipients(void) {
  if (canHandle == NULL) {
    return;
  }
  clocks_local_snapshot_t local;
  clocksStateCopyLocal(&local);

  uint8_t out[CAN_FRAME_MAX_LENGTH] = {};
  if (fiesta_can_put_temp_i16(
          &out[CAN_FRAME_EGT_UPDATE_EGT_HI],
          local.egtValid[EGT_PRE_DPF] ? local.egtC[EGT_PRE_DPF] : NAN)) {
    out[CAN_FRAME_EGT_UPDATE_STATUS] |= CAN_EGT_STATUS_EGT_VALID;
  }
  if (fiesta_can_put_temp_i16(
          &out[CAN_FRAME_EGT_UPDATE_DPF_TEMP_HI],
          local.egtValid[EGT_IN_DPF] ? local.egtC[EGT_IN_DPF] : NAN)) {
    out[CAN_FRAME_EGT_UPDATE_STATUS] |= CAN_EGT_STATUS_DPF_TEMP_VALID;
  }
  canBroadcast(CAN_ID_EGT_UPDATE, out);
}

void updateGPSrecipients(void) {
  if (canHandle == NULL)
    return;
  clocks_local_snapshot_t local = {};
  clocksStateCopyLocal(&local);
  uint8_t lat[CAN_FRAME_MAX_LENGTH], lonTime[CAN_FRAME_MAX_LENGTH];
  uint8_t utc[CAN_FRAME_MAX_LENGTH];
  if (clocksGpsEncode(&local.gps, lat, lonTime, utc) != HAL_OK)
    return;
  canBroadcast(CAN_ID_GPS_EXT_LAT, lat);
  canBroadcast(CAN_ID_GPS_EXT_LON_TIME, lonTime);
  canBroadcast(CAN_ID_GPS_UTC_UPDATE, utc);
}

void receivedCanMessage(void) {
  interrupt = true;
  timingDiagCanInterrupt();
}

static void onCanFrame(uint32_t canID, uint8_t len, const uint8_t *buf) {
  interrupt = false;

  if (buf == NULL || len > CAN_FRAME_MAX_LENGTH) {
    derr("Received invalid CAN frame with ID: %03x, len: %d\n", canID, len);
    return;
  }
  timingDiagCanFrame(canID, buf, len);
  stateChanged = true;

  switch (canID) {
  case CAN_ID_ECU_UPDATE_01: {
    if (len <= CAN_FRAME_ECU_UPDATE_OIL) {
      derr("Received truncated ECU_UPDATE_01 frame with len: %d", len);
      return;
    }
    countEcuFrame();

    putValue(F_CALCULATED_ENGINE_LOAD, buf[CAN_FRAME_ECU_UPDATE_ENGINE_LOAD]);
    putValue(F_VOLTS, fiesta_can_join_decimal_tenths(
                          buf[CAN_FRAME_ECU_UPDATE_VOLTS_HI],
                          buf[CAN_FRAME_ECU_UPDATE_VOLTS_LO]));
    putValue(F_COOLANT_TEMP,
             static_cast<int8_t>(buf[CAN_FRAME_ECU_UPDATE_COOLANT]));
    putValue(F_OIL_TEMP, static_cast<int8_t>(buf[CAN_FRAME_ECU_UPDATE_OIL]));
  } break;

  case CAN_ID_DPF: {
    if (len <= CAN_FRAME_DPF_UPDATE_DPF_REGEN) {
      derr("Received truncated DPF frame with len: %d", len);
      return;
    }
    dpfMessages++;
    putValue(F_DPF_REGEN, buf[CAN_FRAME_DPF_UPDATE_DPF_REGEN]);
  } break;

  case CAN_ID_THROTTLE: {
    if (len <= CAN_FRAME_THROTTLE_UPDATE_LO) {
      derr("Received truncated throttle frame with len: %d", len);
      return;
    }
    countEcuFrame();

    putValue(F_THROTTLE_POS,
             jh_u16_from_bytes(buf[CAN_FRAME_THROTTLE_UPDATE_HI],
                               buf[CAN_FRAME_THROTTLE_UPDATE_LO]));
    requestRedraw();
  } break;

  case CAN_ID_TURBO_PRESSURE: {
    if (len <= CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_LO) {
      derr("Received truncated turbo-pressure frame with len: %d", len);
      return;
    }
    countEcuFrame();

    putValue(F_PRESSURE, fiesta_can_join_decimal_tenths(
                             buf[CAN_FRAME_ECU_UPDATE_PRESSURE_HI],
                             buf[CAN_FRAME_ECU_UPDATE_PRESSURE_LO]));
    putValue(F_PRESSURE_DESIRED,
             fiesta_can_join_decimal_tenths(
                 buf[CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_HI],
                 buf[CAN_FRAME_ECU_UPDATE_PRESSURE_DESIRED_LO]));
    requestRedraw();
  } break;

  case CAN_ID_RPM: {
    if (len <= CAN_FRAME_RPM_UPDATE_LO) {
      derr("Received truncated RPM frame with len: %d", len);
      return;
    }
    countEcuFrame();

    putValue(F_RPM, jh_u16_from_bytes(buf[CAN_FRAME_RPM_UPDATE_HI],
                                      buf[CAN_FRAME_RPM_UPDATE_LO]));
    updateCluster();
    timingDiagRpmApplied();
  } break;

  case CAN_ID_ECU_UPDATE_02: {
    if (len <= CAN_FRAME_ECU_UPDATE_FUEL_LO) {
      derr("Received truncated ECU_UPDATE_02 frame with len: %d", len);
      return;
    }
    countEcuFrame();

    putValue(F_INTAKE_TEMP,
             static_cast<int8_t>(buf[CAN_FRAME_ECU_UPDATE_INTAKE]));
    putValue(F_FUEL, jh_u16_from_bytes(buf[CAN_FRAME_ECU_UPDATE_FUEL_HI],
                                       buf[CAN_FRAME_ECU_UPDATE_FUEL_LO]));
  } break;

  case CAN_ID_ECU_UPDATE_03: {
    if (len <= CAN_FRAME_ECU_UPDATE_FAN_ENABLED) {
      derr("Received truncated ECU_UPDATE_03 frame with len: %d", len);
      return;
    }
    countEcuFrame();

    putValue(F_PRESSURE_PERCENTAGE,
             buf[CAN_FRAME_ECU_UPDATE_PRESSURE_PERCENTAGE]);
    putValue(F_FUEL_TEMP,
             static_cast<int8_t>(buf[CAN_FRAME_ECU_UPDATE_FUEL_TEMP]));
    putValue(F_FAN_ENABLED, buf[CAN_FRAME_ECU_UPDATE_FAN_ENABLED]);
  } break;

  case CAN_ID_OIL_AND_SPEED_MODULE_UPDATE:
  case CAN_ID_EGT_UPDATE:
    // Clocks sends these itself and every ID has one sender: a second one is
    // a wiring or configuration fault, e.g. a retired sender still connected.
    derr_limited("can", "frame 0x%03lx of Clocks came from another node",
                 (unsigned long)canID);
    break;

  case CAN_ID_LUMENS: {
    if (len <= CAN_FRAME_LIGHTS_UPDATE_LO) {
      derr("Received truncated lumens frame with len: %d", len);
      return;
    }
    putValue(F_OUTSIDE_LUMENS,
             fiesta_can_join_decimal_tenths(buf[CAN_FRAME_LIGHTS_UPDATE_HI],
                                            buf[CAN_FRAME_LIGHTS_UPDATE_LO]));
  } break;
  case CAN_ID_GPS_EXT_LAT:
  case CAN_ID_GPS_EXT_LON_TIME:
  case CAN_ID_GPS_UTC_UPDATE:
    // GPS frames of Clocks; another sender is a fault, e.g. an old ECU image.
    derr_limited("can", "GPS frame 0x%03lx of Clocks came from another node",
                 (unsigned long)canID);
    break;

  default:
    deb("received unknown CAN frame:%03x len:%d\n", canID, len);
    break;
  }
}

void canMainLoop(void) {
  if (canHandle == NULL) {
    return;
  }
  const uint32_t drainStart = timingDiagLoopStart();
  const hal_status_t st = hal_can_process_all(canHandle, onCanFrame, NULL);
  timingDiagCanDrained(drainStart);
  if (st != HAL_OK) {
    derr_limited("can", "receive failed: %s", hal_status_to_string(st));
  }
  publishState();
}

bool canRxPending(void) { return interrupt; }

// Brightness, oil pressure and ABS speed every CAN_UPDATE_RECIPIENTS, both
// EGT every CAN_EGT_UPDATE_INTERVAL; never on every loop pass.
static hal_soft_timer_t timerCANUpdate = NULL;
static hal_soft_timer_t timerEGTUpdate = NULL;
static hal_soft_timer_t timerGPSUpdate = NULL;
static const hal_soft_timer_table_entry_t canBroadcastTimerTable[] = {
    {&timerCANUpdate, updateCANrecipients, (uint32_t)CAN_UPDATE_RECIPIENTS},
    {&timerEGTUpdate, updateEGTrecipients, (uint32_t)CAN_EGT_UPDATE_INTERVAL},
    {&timerGPSUpdate, updateGPSrecipients, CAN_GPS_UPDATE_INTERVAL}};

bool canSetupBroadcastTimers(void) {
  return hal_soft_timer_setup_table(canBroadcastTimerTable,
                                    COUNTOF(canBroadcastTimerTable),
                                    hal_watchdog_feed, CORE_OPERATION_DELAY);
}

void canTickBroadcastTimers(void) {
  (void)hal_soft_timer_tick_table(canBroadcastTimerTable,
                                  COUNTOF(canBroadcastTimerTable));
}

float canGetValue(int idx) {
  return (idx >= 0 && idx < F_LAST) ? canValues[idx] : 0.0f;
}

bool canValueFresh(int idx, uint32_t nowMs, uint32_t maxAgeMs) {
  return (idx >= 0 && idx < F_LAST) && valueDecoded[idx] &&
         !hal_elapsed_u32(nowMs, valueUpdatedMs[idx], maxAgeMs);
}

void canSetValue(int idx, float val) {
  if (idx >= 0 && idx < F_LAST) {
    putValue(idx, val);
    stateChanged = true;
  }
}

void canCheckConnection(void) {
  ecuConnected = (ecuMessages != lastEcuMessages);
  lastEcuMessages = ecuMessages;

  dpfConnected = (dpfMessages != lastDPFMessages);
  lastDPFMessages = dpfMessages;

  if (lastConnected != ecuConnected) {
    lastConnected = ecuConnected;

    // Losing the ECU clears its data; the ABS speed is measured here.
    if (!ecuConnected) {
      for (int a = 0; a < F_LAST; a++) {
        if (a != F_ABS_CAR_SPEED) {
          canValues[a] = 0.0;
        }
      }
    }
    requestRedraw();
  }
  stateChanged = true;
  publishState();
}

void canSetAbsReady(bool ready) {
  absReady = ready;
  stateChanged = true;
}
