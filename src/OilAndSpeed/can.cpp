#include "can.h"

float valueFields[F_LAST];
static hal_can_t canHandle = NULL;

static unsigned char frameNumber = 0;
static unsigned long ecuMessages = 0, lastEcuMessages = 0;
static bool ecuConnected = false;
static unsigned long dpfMessages = 0, lastDPFMessages = 0;
static bool dpfConnected = false;
static unsigned long clusterMessages = 0, lastClusterMessages = 0;
static bool clusterConnected = false;

bool canInit(void) {
  ecuConnected = false;
  ecuMessages = lastEcuMessages = 0;
  dpfMessages = lastDPFMessages = 0;

  int canRetries = 0;
  bool error = false;

  while (!(canHandle = hal_can_create(CAN_CS))) {
    watchdog_feed();
    canRetries++;
    if (canRetries == MAX_RETRIES) {
      error = true;
      break;
    }

    deb("ERROR!!!! CAN-BUS Shield init fail\n");
    deb("ERROR!!!! Will try to init CAN-BUS shield again\n");

    m_delay(SECOND);
  }
  if (!error) {
    watchdog_feed();
    deb("CAN BUS Shield init ok!");
    hal_gpio_set_mode(CAN_INT, HAL_GPIO_INPUT);
    canMainLoop();
  }
  return error;
}

void updateCANrecipients(void) {

  uint8_t buf[CAN_FRAME_MAX_LENGTH] = {};

  buf[CAN_FRAME_NUMBER] = frameNumber++;

  int hi, lo;
  floatToDec(valueFields[F_OIL_PRESSURE], &hi, &lo);
  buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI] = (uint8_t)hi;
  buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO] = (uint8_t)lo;
  buf[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED] = (uint8_t)valueFields[F_ABS_CAR_SPEED];

  hal_can_send(canHandle, CAN_ID_OIL_AND_SPEED_MODULE_UPDATE, CAN_FRAME_MAX_LENGTH, buf);
}

static byte lastFrame = 0;
void canMainLoop(void) {
  if (!hal_can_available(canHandle)) {
    return;
  }

  uint32_t canID = 0;
  uint8_t len = 0;
  uint8_t buf[CAN_FRAME_MAX_LENGTH] = {};

  if (!hal_can_receive(canHandle, &canID, &len, buf)) {
    return;
  }

  if (canID == 0 || len < 1) {
    return;
  }

  if (lastFrame != buf[CAN_FRAME_NUMBER]) {
    lastFrame = buf[CAN_FRAME_NUMBER];

    switch (canID) {

      case CAN_ID_ECU_UPDATE_01:
      case CAN_ID_DPF:
      case CAN_ID_CLOCK_BRIGHTNESS:
      case CAN_ID_RPM:
      case CAN_ID_THROTTLE:
      case CAN_ID_ECU_UPDATE_03:
      case CAN_ID_TURBO_PRESSURE:
        ecuMessages++; ecuConnected = true;
        break;

      case CAN_ID_LUMENS:
        clusterMessages++; clusterConnected = true;
        break;

      case CAN_ID_ECU_UPDATE_02: {
        ecuMessages++; ecuConnected = true;

        valueFields[F_INTAKE_TEMP] = buf[CAN_FRAME_ECU_UPDATE_INTAKE];
        valueFields[F_FUEL] = MsbLsbToInt(buf[CAN_FRAME_ECU_UPDATE_FUEL_HI],
                                          buf[CAN_FRAME_ECU_UPDATE_FUEL_LO]);
        valueFields[F_GPS_IS_AVAILABLE] = buf[CAN_FRAME_ECU_UPDATE_GPS_AVAILABLE];
        valueFields[F_GPS_CAR_SPEED] = buf[CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED];
      }
      break;

      default:
        deb("received unknown CAN frame:%03x len:%d\n", canID, len);
        break;
    }
  }
}

bool isClusterConnected(void) {
  return clusterConnected;
}

bool isEcuConnected(void) {
  return ecuConnected;
}

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

bool canSendLoop(void) {
  static float lastSpeed = 0.0;
  static float lastOilPressure = 0.0;

  if (lastSpeed != valueFields[F_ABS_CAR_SPEED]) {
    lastSpeed = valueFields[F_ABS_CAR_SPEED];
    updateCANrecipients();
  }

  if (lastOilPressure != valueFields[F_OIL_PRESSURE]) {
    lastOilPressure = valueFields[F_OIL_PRESSURE];
    updateCANrecipients();
  }

#ifdef ABS_CAR_SPEED_PACKET_TEST
  static int amountCounter = 0;
  static int lastSpeed = 0;
  static unsigned long pauseUntil = 0;

  unsigned long now = hal_millis();

  if (pauseUntil != 0) {
    if (now < pauseUntil) {
      getRandomEverySomeMillis(ABS_CAR_SPEED_SEQUENCE_DELAY, 200);
      return true;
    } else {
      pauseUntil = 0;
    }
  }

  int speed = getRandomEverySomeMillis(ABS_CAR_SPEED_SEQUENCE_DELAY, 200);
  if (lastSpeed != speed) {
    amountCounter++;
    if (amountCounter == 4) {
      amountCounter = 0;
      speed = 0;
      pauseUntil = now + ABS_CAR_SPEED_SEQUENCE_DELAY;
    }
    lastSpeed = speed;
    deb("new speed: %d", speed);
    valueFields[F_ABS_CAR_SPEED] = speed;
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

  valueFields[F_ABS_CAR_SPEED] = val;
#endif

#ifdef OIL_PRESSURE_PACKET_TEST
  static float lastPressure = 0.0f;
  float pressure = getRandomFloatEverySomeMillis(4500, 4.0);
  if (lastPressure != pressure) {
    lastPressure = pressure;
    deb("new pressure: %f", pressure);
    valueFields[F_OIL_PRESSURE] = pressure;
    updateCANrecipients();
  }
#endif

  return true;
}
