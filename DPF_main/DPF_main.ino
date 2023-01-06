
#include <SPI.h>
#include <mcp_can.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char displayError[] PROGMEM = "SSD1306 allocation failed!";
const char hello[] PROGMEM = "Hello!";

void setup() {
  Serial.begin(9600);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { //Ive changed the address //already chill
    Serial.println(F(displayError));
    for(;;); // Don't proceed, loop forever
  }



  display.clearDisplay();

  tx(0, 0, F(hello));
  int h = getTxHeight(F(hello));

  tx(0, h, F("Hello, world - 2"));

  display.display();

  mcp_setup();
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


unsigned int value = 0;
static char disp[16];

void loop() {

  display.fillRect(0, 16, 128, 8, SSD1306_BLACK);

  memset(disp, 0, sizeof(disp));
  snprintf(disp, sizeof(disp) - 1, "count: %ld", value++);
  tx(0, 16, F(disp));
  display.display();
  
  mcp_loop();
}



void mcp_setup() {

}

void mcp_loop()
{

}
