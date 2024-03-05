#include "can.h"

MCP_CAN CAN(&SPI1, CAN_CS);

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
    canRetries++;
    if(canRetries == MAX_RETRIES) {
      error = true;
      break;
    }

    deb("ERROR!!!! CAN-BUS Shield init fail\n");
    deb("ERROR!!!! Will try to init CAN-BUS shield again\n");

    delay(1000);
  }
  if(!error) {
    deb("CAN BUS Shield init ok!");
    CAN.setMode(MCP_NORMAL); 
    CAN.setSleepWakeup(1); // Enable wake up interrupt when in sleep mode
    pinMode(CAN1_INT, INPUT); 
    attachInterrupt(digitalPinToInterrupt(CAN1_INT), receivedCanMessage, FALLING);
  }
  return error;
}

bool callAtHalfSecond(void *argument) {

  //INT8U sendMsgBuf(INT32U id, INT8U len, INT8U *buf); 

  byte buf[CAN_FRAME_MAX_LENGTH];

  buf[CAN_FRAME_NUMBER] = frameNumber++;

  int hi, lo;
  floatToDec(valueFields[F_OUTSIDE_LUMENS], &hi, &lo);
  buf[CAN_FRAME_LIGHTS_UPDATE_HI] = (byte)hi;
  buf[CAN_FRAME_LIGHTS_UPDATE_LO] = (byte)lo;

  CAN.sendMsgBuf(CAN_ID_LUMENS, sizeof(buf), buf);  

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
            }
            break;

            default:
              deb("received unknown CAN frame: %d\n", canID);
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

