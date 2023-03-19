#include "can.h"

MCP_CAN CAN(CAN0_GPIO);

void receivedCanMessage(void);

static byte frameNumber = 0;
// This the eight byte buffer of the incoming message data payload
static byte buf[CAN_FRAME_MAX_LENGTH];

static bool interrupt = false;

// Incoming CAN-BUS message
static long unsigned int canID = 0x000;

// This is the length of the incoming CAN-BUS message
static unsigned char len = 0;

static bool dpfConnected = false;
static unsigned long dpfMessages = 0, lastDPFMessages = 0;
static byte lastFrame = 0;

void canInit(void) {
  int at = 1;

  dpfConnected = false;
  dpfMessages = lastDPFMessages = 0;

  Adafruit_ST7735 tft = returnReference();

  tft.setFont();
  tft.setTextSize(1);

  while(!(CAN_OK == CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ))) {
    deb("ERROR!!!! CAN-BUS Shield init fail");
    deb("Will try to init CAN-BUS shield again");

    tft.fillScreen(ST7735_BLACK);

    int x = 10;
    int y = 10;
    tft.setCursor(x, y);
    tft.println(F("CAN module init fail"));
    y += 10;
    tft.setCursor(x, y);

    char displayTxt[32];

    memset(displayTxt, 0, sizeof(displayTxt));
    snprintf(displayTxt, sizeof(displayTxt) - 1, (const char*)F("Connection attempt: %d"), at++);
    tft.println(displayTxt);

    delay(1000);
    watchdog_update();
  }

  deb("CAN BUS Shield init ok!");
  CAN.setMode(MCP_NORMAL); 
  CAN.setSleepWakeup(1); // Enable wake up interrupt when in sleep mode

  pinMode(CAN0_INT, INPUT); 
  attachInterrupt(digitalPinToInterrupt(CAN0_INT), receivedCanMessage, FALLING);
}

bool updateCANrecipients(void *argument) {

  byte buf[CAN_FRAME_MAX_LENGTH];
  buf[CAN_FRAME_NUMBER] = frameNumber++;
  
  buf[CAN_FRAME_ECU_UPDATE_ENGINE_LOAD] = 
    (byte)getThrottlePercentage((int)valueFields[F_ENGINE_LOAD]);

  short rpm = valueFields[F_RPM];
  buf[CAN_FRAME_ECU_UPDATE_RPM_HI] = (rpm >> 8) & 0xFF;
  buf[CAN_FRAME_ECU_UPDATE_RPM_LO] = rpm & 0xFF;

  buf[CAN_FRAME_ECU_UPDATE_COOLANT] = (byte)valueFields[F_COOLANT_TEMP];
  buf[CAN_FRAME_ECU_UPDATE_OIL] = (byte)valueFields[F_OIL_TEMP];

  short exh = valueFields[F_EGT];
  buf[CAN_FRAME_ECU_UPDATE_EGT_HI] = (exh >> 8) & 0xFF;
  buf[CAN_FRAME_ECU_UPDATE_EGT_LO] = exh & 0xFF;

  CAN.sendMsgBuf(CAN_ID_ECU_UPDATE, sizeof(buf), buf);  

  return true;  
}


void receivedCanMessage(void) {
    interrupt = true;
}

bool canMainLoop(void *argument) {
    CAN.readMsgBuf(&canID, &len, buf);
    if(canID == 0 || len < 1) {
        return true;
    }

    if(lastFrame != buf[CAN_FRAME_NUMBER] || interrupt) {
        interrupt = false;
        lastFrame = buf[CAN_FRAME_NUMBER];

        switch(canID) {
            case CAN_ID_DPF: {
              dpfMessages++;
              valueFields[F_DPF_TEMP] = 
                ((unsigned short)buf[CAN_FRAME_DPF_UPDATE_DPF_TEMP_HI] << 8) | 
                buf[CAN_FRAME_DPF_UPDATE_DPF_TEMP_LO];            
              valueFields[F_DPF_REGEN] = buf[CAN_FRAME_DPF_UPDATE_DPF_REGEN];
            }
            break;

            default:
              deb("received unknown CAN frame: %d\n", canID);
              break;
        }
    }
    return true;
}

bool isDPFConnected(void) {
  return dpfConnected;
}

bool canCheckConnection(void *message) {
  dpfConnected = (dpfMessages != lastDPFMessages);
  lastDPFMessages = dpfMessages;
  return true;  
}



