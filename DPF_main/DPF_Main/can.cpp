#include "can.h"

void canMainLoop(void);
void receivedCanMessage(void);
bool callAtSomeTime(void *argument);

//default for raspberry pi pico: SDA GPIO 4, SCL GPIO 5 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//default for raspberry pi pico: 
//mosi GPIO 19, pin 25/spi0 tx
//sck  GPIO 18, pin 24/spi0 sck
//cs   GPIO 17, pin 22/spi0 cs
//miso GPIO 16, pin 21/spi0 rx 
MCP_CAN CAN(17);

Timer generalTimer;

const char displayError[] PROGMEM = "SSD1306 allocation failed!";
const char dpfError[] PROGMEM = "MCP2515 init problem!";
const char hello[] PROGMEM = "DPF Module";

static int textHeight = 0;
static unsigned char frameNumber = 0;

static bool started = false;

void initialization() {
    Serial.begin(9600);

    pinMode(LED_BUILTIN, OUTPUT);

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F(displayError));
        for(;;); 
    }
    display.clearDisplay();
    tx(0, 0, F(hello));
    textHeight = getTxHeight(F(hello));

    display.display();

    while(!(CAN_OK == CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ))) {
        Serial.println("ERROR!!!! CAN-BUS Shield init fail");
        Serial.println("ERROR!!!! Will try to init CAN-BUS shield again");

        tx(0, textHeight, F(dpfError));
        display.display();    

        delay(1000);
    }

    Serial.println("CAN BUS Shield init ok!");
    CAN.setMode(MCP_NORMAL); 
    CAN.setSleepWakeup(1); // Enable wake up interrupt when in sleep mode
    pinMode(CAN0_INT, INPUT); 
    attachInterrupt(digitalPinToInterrupt(CAN0_INT), receivedCanMessage, FALLING);

    generalTimer = timer_create_default();
    generalTimer.every(500, callAtSomeTime);
    
    started = true;
}

void tx(int x, int y, const __FlashStringHelper *txt) {
    display.setTextSize(1);             // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE);        // Draw white text
    display.setCursor(x, y);             // Start at top-left corner
    display.println(txt);  
}

int getTxHeight(const __FlashStringHelper *txt) {
    uint16_t h;
    display.getTextBounds(txt, 0, 0, NULL, NULL, NULL, &h);
    return h;
}

int getTxWidth(const __FlashStringHelper *txt) {
    uint16_t w;
    display.getTextBounds(txt, 0, 0, NULL, NULL, &w, NULL);
    return w;
}

static char disp[16];
static byte throttle = 0;

void looper() {
    if(!started) {
        return;
    }
    generalTimer.tick();

    canMainLoop();
}

void quickDisplay(int val1, int val2) {
    display.fillRect(0, textHeight, 128, textHeight, SSD1306_BLACK);

    memset(disp, 0, sizeof(disp));
    snprintf(disp, sizeof(disp) - 1, "values: %d %d", val1, val2);
    tx(0, textHeight, F(disp));
    display.display();
}

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



