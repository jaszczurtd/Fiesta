#include "can.h"

//default for raspberry pi pico: 
//mosi GPIO 19, pin 25/spi0 tx
//sck  GPIO 18, pin 24/spi0 sck
//cs   GPIO 17, pin 22/spi0 cs
//miso GPIO 16, pin 21/spi0 rx 
MCP_CAN CAN(17);

const char canError[] PROGMEM = "MCP2515 init problem!";

static unsigned char frameNumber = 0;

void canInit(void) {
    while(!(CAN_OK == CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ))) {
        Serial.println("ERROR!!!! CAN-BUS Shield init fail");
        Serial.println("ERROR!!!! Will try to init CAN-BUS shield again");

        tx(0, getDefaultTextHeight(), F(canError));
        show();

        delay(1000);
    }

    deb("CAN BUS Shield init ok!");
    CAN.setMode(MCP_NORMAL); 
    CAN.setSleepWakeup(1); // Enable wake up interrupt when in sleep mode
    pinMode(CAN1_INT, INPUT); 
    attachInterrupt(digitalPinToInterrupt(CAN1_INT), receivedCanMessage, FALLING);
}

static byte throttle = 0;

bool callAtSomeTime(void *argument) {

    //INT8U sendMsgBuf(INT32U id, INT8U len, INT8U *buf); 

    byte buf[2];

    buf[0] = frameNumber++;
    buf[1] = 117;

    CAN.sendMsgBuf(CAN_ID_DPF, sizeof(buf), buf);  

    quickDisplay(frameNumber, throttle);
    return true; 
}

// Incoming CAN-BUS message
long unsigned int canID = 0x000;

// This is the length of the incoming CAN-BUS message
unsigned char len = 0;

// This the eight byte buffer of the incoming message data payload
static byte buf[8];

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
            case CAN_ID_ENGINE_LOAD: {

                throttle = buf[1];

                deb("%d %d", buf[CAN_FRAME_NUMBER], throttle);

                //quickDisplay(frameNumber, throttle);
            }

            break;

            default:
                deb("received unknown CAN frame: %d\n", canID);
                break;
        }
    }
}



