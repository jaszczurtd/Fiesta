#include "can.h"

#include <utils/multicoreWatchdog.h>

#ifdef UNIT_TEST
hal_can_t oilspeedTestGetCanHandle(void);
#endif

static hal_can_t canHandle = NULL;

#ifdef UNIT_TEST
hal_can_t oilspeedTestGetCanHandle(void) { return canHandle; }
#endif

static unsigned char frameNumber = 0;
static hal_soft_timer_t timerCANUpdate = NULL;
static hal_soft_timer_t timerEGTUpdate = NULL;
static unsigned long ecuMessages = 0, lastEcuMessages = 0;
static bool ecuConnected = false;
static unsigned long dpfMessages = 0, lastDPFMessages = 0;
static bool dpfConnected = false;
static unsigned long clusterMessages = 0, lastClusterMessages = 0;
static bool clusterConnected = false;

hal_status_t canInit(void) {
  ecuConnected = false;
  ecuMessages = lastEcuMessages = 0;
  dpfMessages = lastDPFMessages = 0;

  hal_can_config_t canCfg = hal_can_default_config();
  canCfg.mcp2515.cs_pin = CAN_CS;

  const hal_status_t st = hal_can_create_with_retry(
      &canCfg, CAN_INT, NULL, MAX_RETRIES, watchdog_feed, &canHandle);
  if (st != HAL_OK) {
    derr("CAN BUS Shield init failed: %s", hal_status_to_string(st));
    return st;
  }
  deb("CAN BUS Shield init ok!");
  canMainLoop();
  return HAL_OK;
}

/**
 * @brief Send one broadcast frame; a failure is logged with its reason.
 * @param id CAN identifier.
 * @param buf CAN_FRAME_MAX_LENGTH payload bytes.
 */
static void canBroadcast(uint32_t id, const uint8_t *buf) {
  if (canHandle == NULL) {
    return; /* canInit() failed and said why */
  }
  const hal_status_t st =
      hal_can_send(canHandle, id, CAN_FRAME_MAX_LENGTH, buf);
  if (st != HAL_OK) {
    derr_limited("can", "frame 0x%03lx not sent: %s", (unsigned long)id,
                 hal_status_to_string(st));
  }
}

void updateCANrecipients(void) {

  uint8_t buf[CAN_FRAME_MAX_LENGTH] = {};

  buf[CAN_FRAME_NUMBER] = frameNumber++;

  int hi, lo;
  fiesta_can_split_decimal_tenths(getGlobalValue(F_OIL_PRESSURE), &hi, &lo);
  buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI] = (uint8_t)hi;
  buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO] = (uint8_t)lo;
  buf[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED] =
      (uint8_t)getGlobalValue(F_ABS_CAR_SPEED);

  canBroadcast(CAN_ID_OIL_AND_SPEED_MODULE_UPDATE, buf);
}

void updateEGTrecipients(void) {
  uint8_t buf[CAN_FRAME_MAX_LENGTH] = {};

  buf[CAN_FRAME_NUMBER] = frameNumber++;

  int16_t egt = (int16_t)getGlobalValue(F_EGT);
  buf[CAN_FRAME_EGT_UPDATE_EGT_HI] = MSB(egt);
  buf[CAN_FRAME_EGT_UPDATE_EGT_LO] = LSB(egt);

  int16_t dpfTemp = (int16_t)getGlobalValue(F_DPF_TEMP);
  buf[CAN_FRAME_EGT_UPDATE_DPF_TEMP_HI] = MSB(dpfTemp);
  buf[CAN_FRAME_EGT_UPDATE_DPF_TEMP_LO] = LSB(dpfTemp);

  canBroadcast(CAN_ID_EGT_UPDATE, buf);
}

static void onCanFrame(uint32_t canID, uint8_t len, const uint8_t *buf) {
  if (buf == NULL || len > CAN_FRAME_MAX_LENGTH) {
    derr("Received invalid CAN frame with ID: %03x, len: %d\n", canID, len);
    return;
  }

  switch (canID) {

  case CAN_ID_ECU_UPDATE_01:
  case CAN_ID_RPM:
  case CAN_ID_THROTTLE:
  case CAN_ID_ECU_UPDATE_03:
  case CAN_ID_TURBO_PRESSURE:
    ecuMessages++;
    ecuConnected = true;
    break;

  case CAN_ID_CLOCK_BRIGHTNESS:
    clusterMessages++;
    clusterConnected = true;
    break;

  case CAN_ID_ECU_UPDATE_02: {
    if (len <= CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED) {
      derr("Received truncated ECU_UPDATE_02 frame with len: %d", len);
      return;
    }
    ecuMessages++;
    ecuConnected = true;

    setGlobalValue(F_INTAKE_TEMP, buf[CAN_FRAME_ECU_UPDATE_INTAKE]);
    setGlobalValue(F_FUEL,
                   jh_u16_from_bytes(buf[CAN_FRAME_ECU_UPDATE_FUEL_HI],
                                     buf[CAN_FRAME_ECU_UPDATE_FUEL_LO]));
    setGlobalValue(F_GPS_IS_AVAILABLE, buf[CAN_FRAME_ECU_UPDATE_GPS_AVAILABLE]);
    setGlobalValue(F_GPS_CAR_SPEED, buf[CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED]);
  } break;

  default:
    deb("received unknown CAN frame:%03x len:%d\n", canID, len);
    break;
  }
}

void canMainLoop(void) {
  if (canHandle == NULL) {
    return;
  }
  const hal_status_t st = hal_can_process_all(canHandle, onCanFrame, NULL);
  if (st != HAL_OK) {
    derr_limited("can", "receive failed: %s", hal_status_to_string(st));
  }
}

bool isClusterConnected(void) { return clusterConnected; }

bool isEcuConnected(void) { return ecuConnected; }

void canCheckConnection(void) {
  static int lastColor = 0;
  static bool state = false;

  ecuConnected = (ecuMessages != lastEcuMessages);
  lastEcuMessages = ecuMessages;

  dpfConnected = (dpfMessages != lastDPFMessages);
  lastDPFMessages = dpfMessages;

  clusterConnected = (clusterMessages != lastClusterMessages);
  lastClusterMessages = clusterMessages;

  int color = GREEN;
  if (!clusterConnected && ecuConnected) {
    color = (state) ? GREEN : PURPLE;
  }
  if (clusterConnected && !ecuConnected) {
    color = (state) ? GREEN : YELLOW;
  }
  if (!clusterConnected && !ecuConnected) {
    color = (state) ? GREEN : RED;
  }

  state = !state;
  if (color != lastColor) {
    lastColor = color;
    setLEDColor(color);
  }
}

// Both frames double as the module heartbeat: Clocks and the ECU count them
// once per CAN_CHECK_CONNECTION. Sending them every loop pass took about half
// of the 500 kbit/s bus for values that change at 1-10 Hz.
static const hal_soft_timer_table_entry_t canBroadcastTimerTable[] = {
    {&timerCANUpdate, updateCANrecipients, (uint32_t)CAN_UPDATE_RECIPIENTS},
    {&timerEGTUpdate, updateEGTrecipients, (uint32_t)CAN_EGT_UPDATE_INTERVAL}};

bool canSetupBroadcastTimers(void) {
  return hal_soft_timer_setup_table(canBroadcastTimerTable,
                                    COUNTOF(canBroadcastTimerTable),
                                    watchdog_feed, CORE_OPERATION_DELAY);
}

void canTickBroadcastTimers(void) {
  (void)hal_soft_timer_tick_table(canBroadcastTimerTable,
                                  COUNTOF(canBroadcastTimerTable));
}

void canSendLoop(void) {
  // Bench packet generators only; the periodic frames run on the broadcast
  // timers.
#ifdef ABS_CAR_SPEED_PACKET_TEST
  static int amountCounter = 0;
  static int lastSpeed = 0;
  static unsigned long pauseUntil = 0;
  static hal_periodic_random_int_t speedRandom = {};

  unsigned long now = hal_millis();
  int speed = -1;

  if (pauseUntil != 0) {
    if (now < pauseUntil) {
      (void)hal_periodic_random_int_get_ex(
          &speedRandom, now, ABS_CAR_SPEED_SEQUENCE_DELAY, 200, &speed);
      return;
    } else {
      pauseUntil = 0;
    }
  }

  (void)hal_periodic_random_int_get_ex(
      &speedRandom, now, ABS_CAR_SPEED_SEQUENCE_DELAY, 200, &speed);
  if (lastSpeed != speed) {
    amountCounter++;
    if (amountCounter == 4) {
      amountCounter = 0;
      speed = 0;
      pauseUntil = now + ABS_CAR_SPEED_SEQUENCE_DELAY;
    }
    lastSpeed = speed;
    deb("new speed: %d", speed);
    setGlobalValue(F_ABS_CAR_SPEED, speed);
    updateCANrecipients();
  }
#endif

#ifdef ABS_CAR_SPEED_PACKET_LINEAR_TEST
  static int val = 20;
  static unsigned long lastUpdate = 0;

  unsigned long current = hal_millis();

  if (current - lastUpdate >= ABS_CAR_SPEED_SEQUENCE_DELAY) {
    lastUpdate = current;

    val += 10;
    if (val > 220) {
      val = 20;
    }
  }

  setGlobalValue(F_ABS_CAR_SPEED, val);
#endif

#ifdef OIL_PRESSURE_PACKET_TEST
  static float lastPressure = 0.0f;
  static hal_periodic_random_float_t pressureRandom = {};
  float pressure = -1.0f;
  (void)hal_periodic_random_float_get_ex(&pressureRandom, hal_millis(), 4500u,
                                         4.0f, &pressure);
  if (lastPressure != pressure) {
    lastPressure = pressure;
    deb("new pressure: %f", pressure);
    setGlobalValue(F_OIL_PRESSURE, pressure);
    updateCANrecipients();
  }
#endif
}
