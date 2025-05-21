#include "can.h"

static MCP_CAN *CAN = NULL;

float valueFields[F_LAST];

static unsigned char frameNumber = 0;
static unsigned long ecuMessages = 0, lastEcuMessages = 0;
static bool ecuConnected = false;
static unsigned long dpfMessages = 0, lastDPFMessages = 0;
static bool dpfConnected = false;
static unsigned long clusterMessages = 0, lastClusterMessages = 0;
static bool clusterConnected = false;

// Incoming CAN-BUS message
static long unsigned int canID = 0x000;

// This is the length of the incoming CAN-BUS message
static unsigned char len = 0;

// This the eight byte buffer of the incoming message data payload
static byte buf[CAN_FRAME_MAX_LENGTH];

static bool interrupt = false;

bool canInit(void) {
  CAN = new MCP_CAN(CAN_CS);
  ecuConnected = false;
  ecuMessages = lastEcuMessages = 0;
  dpfMessages = lastDPFMessages = 0;
  interrupt = false;

  int canRetries = 0;
  bool error = false;

  while(!(CAN_OK == CAN->begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ))) {
    watchdog_feed();
    canRetries++;
    if(canRetries == MAX_RETRIES) {
      error = true;
      break;
    }

    deb("ERROR!!!! CAN-BUS Shield init fail\n");
    deb("ERROR!!!! Will try to init CAN-BUS shield again\n");

    m_delay(SECOND);
  }
  if(!error) {
    watchdog_feed();
    deb("CAN BUS Shield init ok!");
    CAN->setMode(MCP_NORMAL); 
    CAN->setSleepWakeup(1); // Enable wake up interrupt when in sleep mode
    pinMode(CAN_INT, INPUT); 
    attachInterrupt(digitalPinToInterrupt(CAN_INT), receivedCanMessage, FALLING);
    canMainLoop(NULL);
  }
  return error;
}

bool updateCANrecipients(void *argument) {

  //INT8U sendMsgBuf(INT32U id, INT8U len, INT8U *buf); 

  byte buf[CAN_FRAME_MAX_LENGTH];

  buf[CAN_FRAME_NUMBER] = frameNumber++;

  int hi, lo;
  floatToDec(valueFields[F_OIL_PRESSURE], &hi, &lo);
  buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI] = (byte)hi;
  buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO] = (byte)lo;      
  buf[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED] = (byte)valueFields[F_ABS_CAR_SPEED];

  CAN->sendMsgBuf(CAN_ID_OIL_AND_SPEED_MODULE_UPDATE, CAN_FRAME_MAX_LENGTH, buf);

  return true; 
}

void receivedCanMessage(void) {
  interrupt = true;
}

static byte lastFrame = 0;
bool canMainLoop(void *message) {
  CAN->readMsgBuf(&canID, &len, buf);
  if(canID == 0 || len < 1) {
    return true;
  }

  if(lastFrame != buf[CAN_FRAME_NUMBER] || interrupt) {
    interrupt = false;
    lastFrame = buf[CAN_FRAME_NUMBER];

    switch(canID) {

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
  return true;
}

bool isClusterConnected(void) {
  return clusterConnected;
}

bool isEcuConnected(void) {
  return ecuConnected;
}

bool canCheckConnection(void *message) {
  static int lastColor = 0;
  static bool state = false;

  ecuConnected = (ecuMessages != lastEcuMessages);
  lastEcuMessages = ecuMessages;

  dpfConnected = (dpfMessages != lastDPFMessages);
  lastDPFMessages = dpfMessages;

  clusterConnected = (clusterMessages != lastClusterMessages);
  lastClusterMessages = clusterMessages;

  int color = GREEN;
  if(!clusterConnected && ecuConnected) {
    color = (state) ? GREEN : PURPLE;
  }
  if(clusterConnected && !ecuConnected) {
    color = (state) ? GREEN : YELLOW;
  }
  if(!clusterConnected && !ecuConnected) {
    color = (state) ? GREEN : RED;
  }
  
  state = !state;
  if(color != lastColor) {
    lastColor = color;
    setLEDColor(color);
  }

  return true;  
}

bool canSendLoop(void) {
  static float lastSpeed = 0.0;
  static float lastOilPressure = 0.0;

  if(lastSpeed != valueFields[F_ABS_CAR_SPEED]) {
    lastSpeed = valueFields[F_ABS_CAR_SPEED];
    updateCANrecipients(nullptr);
  }

  if(lastOilPressure != valueFields[F_OIL_PRESSURE]) {
    lastOilPressure = valueFields[F_OIL_PRESSURE];
    updateCANrecipients(nullptr);
  }

#ifdef ABS_CAR_SPEED_PACKET_TEST
  static int amountCounter = 0;
  static int lastSpeed = 0;
  static unsigned long pauseUntil = 0;

  unsigned long now = millis();  

  if (pauseUntil != 0) {
    if (now < pauseUntil) {
      getRandomEverySomeMillis(ABS_CAR_SPEED_SEQUENCE_DELAY, 200);
      return true; 
    } else {
      pauseUntil = 0; 
    }
  }

  int speed = getRandomEverySomeMillis(ABS_CAR_SPEED_SEQUENCE_DELAY, 200);
  if(lastSpeed != speed) {
    amountCounter++;
    if(amountCounter == 4) {
      amountCounter = 0;
      speed = 0;
      pauseUntil = now + ABS_CAR_SPEED_SEQUENCE_DELAY;
    }
    lastSpeed = speed;
    deb("new speed: %d", speed);
    valueFields[F_ABS_CAR_SPEED] = speed;
    updateCANrecipients(NULL);
  }
#endif

#ifdef ABS_CAR_SPEED_PACKET_LINEAR_TEST
  static int val = 20;
  static unsigned long lastUpdate = 0;  

  unsigned long current = millis();

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
  if(lastPressure != pressure) {
    lastPressure = pressure;
    deb("new pressure: %f", pressure);
    valueFields[F_OIL_PRESSURE] = pressure;
    updateCANrecipients(NULL);
  }
#endif


  return true;
}


