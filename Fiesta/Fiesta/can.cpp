#include "can.h"

MCP_CAN CAN(CAN0_GPIO);

void receivedCanMessage(void);

void canInit(void) {
  int at = 1;

  Adafruit_ST7735 tft = returnReference();

  tft.setFont();
  tft.setTextSize(1);

  while(!(CAN_OK == CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ))) {
        Serial.println("ERROR!!!! CAN-BUS Shield init fail");
        Serial.println("ERROR!!!! Will try to init CAN-BUS shield again");

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
    }

    Serial.println("CAN BUS Shield init ok!");
    CAN.setMode(MCP_NORMAL); 
    CAN.setSleepWakeup(1); // Enable wake up interrupt when in sleep mode

    pinMode(CAN0_INT, INPUT); 
    attachInterrupt(digitalPinToInterrupt(CAN0_INT), receivedCanMessage, FALLING);
}

// Incoming CAN-BUS message
long unsigned int canID = 0x000;

// This is the length of the incoming CAN-BUS message
unsigned char len = 0;

// This the eight byte buffer of the incoming message data payload
unsigned char buf[8];

bool interrupt = false;
void receivedCanMessage(void) {
    interrupt = true;
}

static byte lastFrame = 0;
void canMainLoop(void) {
    CAN.readMsgBuf(&canID, &len, buf);
    if(canID == 0 || len < 1) {
        return;
    }

    if(lastFrame != buf[CAN_FRAME_NUMBER] || interrupt) {
        interrupt = false;
        lastFrame = buf[CAN_FRAME_NUMBER];

        switch(canID) {
            case CAN_ID_DPF:

                deb("%d %d", buf[CAN_FRAME_NUMBER], buf[1]);

            break;

            default:
                deb("received unknown CAN frame: %d\n", canID);
                break;
        }
    }
}

