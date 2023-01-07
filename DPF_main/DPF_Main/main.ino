
#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define CAN0_INT 13
#define DPF_CAN_ID 0x123

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

//default for raspberry pi pico: SDA GPIO 4, SCL GPIO 5 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//default for raspberry pi pico: 
//mosi GPIO 19, pin 25/spi0 tx
//sck  GPIO 18, pin 24/spi0 sck
//cs   GPIO 17, pin 22/spi0 cs
//miso GPIO 16, pin 21/spi0 rx 
MCP_CAN CAN(17);

const char displayError[] PROGMEM = "SSD1306 allocation failed!";
const char dpfError[] PROGMEM = "MCP2515 init problem!";
const char hello[] PROGMEM = "DPF Module";

static int textHeight = 0;

void setup() {
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
  pinMode(CAN0_INT, INPUT); 

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


unsigned char value = 0;
static char disp[16];

void loop() {

  display.fillRect(0, textHeight, 128, textHeight, SSD1306_BLACK);

  memset(disp, 0, sizeof(disp));
  snprintf(disp, sizeof(disp) - 1, "count: %d", value++);
  tx(0, textHeight, F(disp));
  display.display();
  
  //INT8U sendMsgBuf(INT32U id, INT8U len, INT8U *buf); 
  
  byte buf[2];

  buf[0] = 12;
  buf[1] = value;

  CAN.sendMsgBuf(DPF_CAN_ID, sizeof(buf), buf);  

  delay(1000);

}



