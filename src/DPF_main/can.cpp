#include "can.h"

//default for raspberry pi pico: 
//mosi GPIO 19, pin 25/spi0 tx
//sck  GPIO 18, pin 24/spi0 sck
//cs   GPIO 17, pin 22/spi0 cs
//miso GPIO 16, pin 21/spi0 rx 
MCP_CAN CAN(17);

const char canError[] PROGMEM = "MCP2515 init problem!";

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

void canInit(void) {
  ecuConnected = false;
  ecuMessages = lastEcuMessages = 0;
  interrupt = false;

  while(!(CAN_OK == CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ))) {
    deb("ERROR!!!! CAN-BUS Shield init fail\n");
    deb("ERROR!!!! Will try to init CAN-BUS shield again\n");

    tx(0, getDefaultTextHeight(), F(canError));
    show();

    delay(1000);
    watchdog_update();
  }

  deb("CAN BUS Shield init ok!");
  CAN.setMode(MCP_NORMAL); 
  CAN.setSleepWakeup(1); // Enable wake up interrupt when in sleep mode
  pinMode(CAN1_INT, INPUT); 
  attachInterrupt(digitalPinToInterrupt(CAN1_INT), receivedCanMessage, FALLING);
}

bool callAtHalfSecond(void *argument) {

  //INT8U sendMsgBuf(INT32U id, INT8U len, INT8U *buf); 

  byte buf[CAN_FRAME_MAX_LENGTH];

  buf[CAN_FRAME_NUMBER] = frameNumber++;

  short temp = valueFields[F_DPF_TEMP];
  buf[CAN_FRAME_DPF_UPDATE_DPF_TEMP_HI] = MSB(temp);
  buf[CAN_FRAME_DPF_UPDATE_DPF_TEMP_LO] = LSB(temp);

  int hi, lo;
  floatToDec(valueFields[F_DPF_PRESSURE], &hi, &lo);
  buf[CAN_FRAME_DPF_UPDATE_DPF_PRESSURE_HI] = (byte)hi;
  buf[CAN_FRAME_DPF_UPDATE_DPF_PRESSURE_LO] = (byte)lo;

  buf[CAN_FRAME_DPF_UPDATE_DPF_REGEN] = isDPFOperating();

  CAN.sendMsgBuf(CAN_ID_DPF, sizeof(buf), buf);  

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
            case CAN_ID_ECU_UPDATE_01: {
              ecuMessages++; ecuConnected = true;

              valueFields[F_CALCULATED_ENGINE_LOAD] = buf[CAN_FRAME_ECU_UPDATE_ENGINE_LOAD];
              valueFields[F_VOLTS] = decToFloat(buf[CAN_FRAME_ECU_UPDATE_VOLTS_HI],
                                                  buf[CAN_FRAME_ECU_UPDATE_VOLTS_LO]);
              valueFields[F_COOLANT_TEMP] = buf[CAN_FRAME_ECU_UPDATE_COOLANT];
              valueFields[F_OIL_TEMP] = buf[CAN_FRAME_ECU_UPDATE_OIL];
              valueFields[F_EGT] = MsbLsbToInt(buf[CAN_FRAME_ECU_UPDATE_EGT_HI],
                                                buf[CAN_FRAME_ECU_UPDATE_EGT_LO]);
            }
            break;

            case CAN_ID_THROTTLE: {
              ecuMessages++; ecuConnected = true;

              valueFields[F_THROTTLE_POS] = MsbLsbToInt(buf[CAN_FRAME_THROTTLE_UPDATE_HI],
                                                        buf[CAN_FRAME_THROTTLE_UPDATE_LO]);
            }
            break;

            case CAN_ID_RPM: {
              ecuMessages++; ecuConnected = true;

              valueFields[F_RPM] = MsbLsbToInt(buf[CAN_FRAME_RPM_UPDATE_HI],
                                                buf[CAN_FRAME_RPM_UPDATE_LO]);
            }
            break;
            
            case CAN_ID_ECU_UPDATE_02: {
              ecuMessages++; ecuConnected = true;

              valueFields[F_INTAKE_TEMP] = buf[CAN_FRAME_ECU_UPDATE_INTAKE];
              valueFields[F_PRESSURE] = decToFloat(buf[CAN_FRAME_ECU_UPDATE_PRESSURE_HI],
                                                    buf[CAN_FRAME_ECU_UPDATE_PRESSURE_LO]);
              valueFields[F_FUEL] = MsbLsbToInt(buf[CAN_FRAME_ECU_UPDATE_FUEL_HI],
                                                buf[CAN_FRAME_ECU_UPDATE_FUEL_LO]);
              valueFields[F_IS_GPS_AVAILABLE] = buf[CAN_FRAME_ECU_UPDATE_GPS_AVAILABLE];
              valueFields[F_CAR_SPEED] = buf[CAN_FRAME_ECU_UPDATE_VEHICLE_SPEED];
            }
            break;

            case CAN_ID_ECU_UPDATE_03: {
              ecuMessages++; ecuConnected = true;

              valueFields[F_PRESSURE_PERCENTAGE] = buf[CAN_FRAME_ECU_UPDATE_PRESSURE_PERCENTAGE];
              valueFields[F_FUEL_TEMP] = buf[CAN_FRAME_ECU_UPDATE_FUEL_TEMP];
              valueFields[F_FAN_ENABLED] = buf[CAN_FRAME_ECU_UPDATE_FAN_ENABLED];
            }
            break;

            case CAN_ID_OIL_PRESURE: {
              valueFields[F_OIL_PRESSURE] = decToFloat(buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI],
                                                       buf[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO]);
            }
            break;

            case CAN_ID_LUMENS:
            case CAN_ID_CLOCK_BRIGHTNESS:
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

