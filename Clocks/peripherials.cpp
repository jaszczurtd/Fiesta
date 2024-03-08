#include "Arduino.h"

#include "peripherials.h"

Adafruit_NeoPixel pixels(NUMPIXELS, PIN_RGB, NEO_GRB + NEO_KHZ800);

float valueFields[F_LAST];

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
  lcdBrightness(INITIAL_BRIGHTNESS);
}

void lcdBrightness(int val) {
  analogWrite(PIN_BRIGHTNESS, ((1 << PWM_WRITE_RESOLUTION) - 1) - val);
}

int getThrottlePercentage(void) {
  int currentVal = int(valueFields[F_THROTTLE_POS]);
  float percent = (currentVal * 100) / PWM_RESOLUTION;
  return percentToGivenVal(percent, 100);
}

void setLEDColor(int ledColor) {
  switch (ledColor) {
    case NONE:
      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();
      break;
    case RED:
      pixels.setPixelColor(0, pixels.Color(20, 0, 0));
      pixels.show();
      break;
    case GREEN:
      pixels.setPixelColor(0, pixels.Color(0, 20, 0));
      pixels.show();
      break;
    case BLUE:
      pixels.setPixelColor(0, pixels.Color(0, 0, 20));
      pixels.show();
      break;
    default:
      break;
  }
}
