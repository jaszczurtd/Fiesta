#include "peripherials.h"

static Adafruit_NeoPixel pixels(NUMPIXELS, PIN_RGB, NEO_RGB + NEO_KHZ800);

void setupOnboardLed(void) {
  pixels.begin();
  setLEDColor(BLUE);
}

void initSPI(void) {
  pinMode(CAN_CS, OUTPUT);
  digitalWrite(CAN_CS, HIGH);

  SPI.setRX(PIN_MISO); //MISO
  SPI.setTX(PIN_MOSI); //MOSI
  SPI.setSCK(PIN_SCK); //SCK
  SPI.begin(true);
}

void initBasicPIO(void) {
  analogWriteResolution(PWM_WRITE_RESOLUTION);
}

void setLEDColor(int ledColor) {
  switch (ledColor) {
    case NONE:
      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();
      break;
    case RED:
      pixels.setPixelColor(0, pixels.Color(30, 0, 0));
      pixels.show();
      break;
    case GREEN:
      pixels.setPixelColor(0, pixels.Color(0, 30, 0));
      pixels.show();
      break;
    case BLUE:
      pixels.setPixelColor(0, pixels.Color(0, 0, 30));
      pixels.show();
      break;
    case YELLOW:
      pixels.setPixelColor(0, pixels.Color(30, 30, 0));
      pixels.show();
      break;
    case WHITE:
      pixels.setPixelColor(0, pixels.Color(30, 30, 30));
      pixels.show();
      break;
    default:
      break;
  }
}

