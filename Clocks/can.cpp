#include "can.h"

MCP_CAN CAN(CAN_CS);

static unsigned char frameNumber = 0;
static unsigned long ecuMessages = 0, lastEcuMessages = 0;
static bool ecuConnected = false;

// Incoming CAN-BUS message
static long unsigned int canID = 0x000;

// This is the length of the incoming CAN-BUS message
static unsigned char len = 0;

// This the eight byte buffer of the incoming message data payload
static byte buf[CAN_FRAME_MAX_LENGTH];

static bool interrupt = false;

bool canInit(void) {
  ecuConnected = false;
  ecuMessages = lastEcuMessages = 0;
  interrupt = false;

  int canRetries = 0;
  bool error = false;

  while(!(CAN_OK == CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ))) {
    watchdog_feed();
    canRetries++;
    if(canRetries == MAX_RETRIES) {
      error = true;
      break;
    }

    deb("ERROR!!!! CAN-BUS Shield init fail\n");
    deb("ERROR!!!! Will try to init CAN-BUS shield again\n");

    m_delay(1000);
  }
  if(!error) {
    deb("CAN BUS Shield init ok!");
    CAN.setMode(MCP_NORMAL); 
    CAN.setSleepWakeup(1); // Enable wake up interrupt when in sleep mode
    pinMode(CAN_INT, INPUT); 
    attachInterrupt(digitalPinToInterrupt(CAN_INT), receivedCanMessage, FALLING);
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

  CAN.sendMsgBuf(CAN_ID_CLOCK_BRIGHTNESS, sizeof(buf), buf);  

  return true; 
}

void receivedCanMessage(void) {
    interrupt = true;
}

static byte lastFrame = 0;
bool canMainLoop(void *message) {
    CAN.readMsgBuf(&canID, &len, buf);
    if(canID == 0 || len < 1) {
        return true;
    }

    if(lastFrame != buf[CAN_FRAME_NUMBER] || interrupt) {
        interrupt = false;
        lastFrame = buf[CAN_FRAME_NUMBER];

        switch(canID) {
            case CAN_ID_ECU_UPDATE: {
              ecuMessages++;

              valueFields[F_CALCULATED_ENGINE_LOAD] = buf[CAN_FRAME_ECU_UPDATE_ENGINE_LOAD];
              valueFields[F_VOLTS] = MsbLsbToInt(buf[CAN_FRAME_ECU_UPDATE_VOLTS_HI],
                                                 buf[CAN_FRAME_ECU_UPDATE_VOLTS_LO]);
              valueFields[F_COOLANT_TEMP] = buf[CAN_FRAME_ECU_UPDATE_COOLANT];
              valueFields[F_OIL_TEMP] = buf[CAN_FRAME_ECU_UPDATE_OIL];
              valueFields[F_EGT] = MsbLsbToInt(buf[CAN_FRAME_ECU_UPDATE_EGT_HI],
                                               buf[CAN_FRAME_ECU_UPDATE_EGT_LO]);

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

bool canCheckConnection(void *message) {
  ecuConnected = (ecuMessages != lastEcuMessages);
  lastEcuMessages = ecuMessages;
  return true;  
}

bool isFanEnabled(void) {
  return false;
}

bool isDPFRegenerating(void) {
  return valueFields[F_DPF_REGEN] > 0;
}

bool isDPFConnected(void) {
  return false;
}

float readFuel(void) {
  return 0.0;
}

int getCurrentCarSpeed(void) {
  return 0;
}

bool isGPSAvailable(void) {
  return false;
}
