#include "peripherials.h"
#include <Wire.h>  // required by ArtronShop_BH1750 constructor (&Wire)

Adafruit_NeoPixel pixels(NUMPIXELS, PIN_RGB, NEO_GRB + NEO_KHZ800);
ArtronShop_BH1750 bh1750(BH1750_ADDR, &Wire); // Non Jump ADDR: 0x23, Jump ADDR: 0x5C
float valueFields[F_LAST];

void initSPI(void) {
  pinMode(CAN_CS, OUTPUT);
  digitalWrite(CAN_CS, HIGH);

  hal_spi_init(1, PIN_MISO, PIN_MOSI, PIN_SCK);
}

void initI2C(void) {
  hal_i2c_init(PIN_SDA, PIN_SCL, I2C_SPEED_HZ);
}

bool setupPeripherials(void) {

  debugInit();
  initSPI();

  pixels.begin();
  setLEDColor(BLUE);

  analogReadResolution(ADC_BITS);
  analogWriteResolution(ANALOG_WRITE_RESOLUTION);

  initI2C();

  deb("starting setup");

  bool error = false;
  int bh1750Retries = 0;
  while (!bh1750.begin()) {
    bh1750Retries++;
    if(bh1750Retries == MAX_RETRIES) {
      error = true;
      break;
    }
    deb("BH1750 not found !");
    delay(1000);
  }
  if(!error) {
    error = canInit();
  }

  setLEDColor((error) ? RED : GREEN);

  deb("setup end");

  return error;
}

float getLumens(void) {
  return bh1750.light();
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

float getSystemVoltage(void) {
  int v = getAverageValueFrom(A0);
  
  float dividerVoltge = (v * ADC_VREF) / ((1 << ADC_BITS) - 1);
  float local_voltage = dividerVoltge * (R1 + R2) / R2;  
  return roundfWithPrecisionTo(local_voltage, 1);
}

int getValuePot(void) {
  return getAverageValueFrom(A1);
}
