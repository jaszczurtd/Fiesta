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
        deb("ERROR!!!! CAN-BUS Shield init fail\n");
        deb("ERROR!!!! Will try to init CAN-BUS shield again\n");

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

bool callAtHalfSecond(void *argument) {

    //INT8U sendMsgBuf(INT32U id, INT8U len, INT8U *buf); 

    byte buf[5];

    buf[0] = frameNumber++;

    short temp = valueFields[F_DPF_TEMP];
    buf[1] = (temp >> 8) & 0xFF;
    buf[2] = temp & 0xFF;

    int hi, lo;
    floatToDec(valueFields[F_DPF_PRESSURE], &hi, &lo);
    buf[3] = (byte)hi;
    buf[4] = (byte)lo;

    CAN.sendMsgBuf(CAN_ID_DPF, sizeof(buf), buf);  

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
            case CAN_ID_ECU_UPDATE: {
                valueFields[F_ENGINE_LOAD] = buf[1];
            }
            break;



            default:
                deb("received unknown CAN frame: %d\n", canID);
                break;
        }
    }
}



