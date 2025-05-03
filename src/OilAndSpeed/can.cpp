#include "can.h"

static MCP_CAN *CAN = NULL;

float valueFields[F_LAST];

static unsigned char frameNumber = 0;
static unsigned long ecuMessages = 0, lastEcuMessages = 0;
static bool ecuConnected = false;
static unsigned long dpfMessages = 0, lastDPFMessages = 0;
static bool dpfConnected = false;

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

  unsigned short br = (unsigned short)valueFields[F_CLOCK_BRIGHTNESS];
  buf[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_HI] = MSB(br);
  buf[CAN_FRAME_CLOCK_BRIGHTNESS_UPDATE_LO] = LSB(br);

  CAN->sendMsgBuf(CAN_ID_CLOCK_BRIGHTNESS, CAN_FRAME_MAX_LENGTH, buf);

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
      case CAN_ID_ECU_UPDATE_02: {
        ecuMessages++; ecuConnected = true;

        valueFields[F_INTAKE_TEMP] = buf[CAN_FRAME_ECU_UPDATE_INTAKE];
        valueFields[F_FUEL] = MsbLsbToInt(buf[CAN_FRAME_ECU_UPDATE_FUEL_HI],
                                          buf[CAN_FRAME_ECU_UPDATE_FUEL_LO]);
        valueFields[F_IS_GPS_AVAILABLE] = buf[CAN_FRAME_ECU_UPDATE_GPS_AVAILABLE];
        valueFields[F_CAR_SPEED] = buf[CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED];
      }
      break;

      case CAN_ID_OIL_PRESURE: {
        valueFields[F_OIL_PRESSURE] = decToFloat(buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI],
                                                  buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO]);
      }
      break;

      default:
        deb("received unknown CAN frame:%03x len:%d\n", canID, len);

        break;
    }
  }
  return true;
}

bool isEcuConnected(void) {
  return ecuConnected;
}

static bool lastConnected = false;
bool canCheckConnection(void *message) {
  ecuConnected = (ecuMessages != lastEcuMessages);
  lastEcuMessages = ecuMessages;

  dpfConnected = (dpfMessages != lastDPFMessages);
  lastDPFMessages = dpfMessages;

  if(lastConnected != ecuConnected) {
    lastConnected = ecuConnected;

    if(!ecuConnected) {
      for(int a = 0; a < F_LAST; a++) {
        valueFields[a] = 0.0;
      }
    }
  }

  return true;  
}

int getCurrentCarSpeed(void) {
  return int(valueFields[F_CAR_SPEED]);
}

