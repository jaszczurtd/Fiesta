#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <Arduino.h>

#include "graphics.h"

#define TFT_CS     2
#define TFT_RST    0  // you can also connect this to the Arduino reset
                      // in which case, set this #define pin to 0!
#define TFT_DC     3

// OPTION 1 (recommended) is to use the HARDWARE SPI pins, which are unique
// to each board and not reassignable. For Arduino Uno: MOSI = pin 11 and
// SCLK = pin 13. This is the fastest mode of operation and is required if
// using the breakout board's microSD card.

// For 1.44" and 1.8" TFT with ST7735 use:
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

Adafruit_ST7735 returnReference(void) {
  return tft;
}

void setup(void) {
  Serial.begin(9600);

  // Use this initializer if using a 1.8" TFT screen:
  tft.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
  tft.setRotation(1);

  Serial.println(F("Initialized"));

  tft.fillScreen(ST7735_BLACK);
  drawImage(0, 0, 160, 128, (unsigned int*)FiestaLogo);
  delay(2000);
  tft.fillScreen(ST7735_BLACK);
}

static int a = 0, b = 0, c = 0, d = 0;
static char msg[128];

unsigned char vall = 0;

void function() {
  uint16_t time = millis();
  time = millis() - time;

  Serial.println(time, DEC);
 
  double t1 = ntcToTemp(A1, 1506, 1500);
  doubleToDec(t1, &a, &b);

  double t2 = ds18b20ToTemp(4, 0);
  doubleToDec(t2, &c, &d);

  vall++;
  if(vall == 256) {
    vall = 0;
  }

  valToPWM(9, vall);
  valToPWM(10, 255 - vall);

  memset(msg, 0, sizeof(msg));
  snprintf(msg, sizeof(msg) - 1, "temp: %d.%d %d.%d %d", a, b, c, d, vall);

  tft.fillRect(0, 0, 120, 20, ST77XX_BLACK);
  tft.setCursor(1, 1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.println(msg);


  int y = 12;
  int x = 3;

  int size = 14;
  for(int a = 0; a < 7; a++) {

    tft.fillRect(x, y, size, size, ST77XX_RED);

    tft.setTextColor(ST7735_ORANGE);
    tft.setCursor(x + size + 2, y + 2);
    tft.setTextSize(1);
    tft.println("jakis text");

    y += size;
    y += 2;

  }

}

void loop() {

  function();
  delay(50);
}


