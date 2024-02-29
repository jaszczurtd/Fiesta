 
#include <Adafruit_NeoPixel.h>
#include <ArtronShop_BH1750.h>
#include <Wire.h>
#include <tools.h>

#define PIN_RGB 16
#define PIN_LAMP 2

#define NUMPIXELS 1

#define ADC_BITS 12
#define PIN_SDA 0
#define PIN_SCL 1
#define I2C_SPEED_HZ 50000

#define MAX_RETRIES 3

#define ANALOG_WRITE_RESOLUTION 9

#define MAX_VOLTAGE 16.0

#define ADC_VREF 3.3
#define R1 3300 // R up (3.3kΩ)
#define R2 470 // R down (470Ω)

Adafruit_NeoPixel pixels(NUMPIXELS, PIN_RGB, NEO_GRB + NEO_KHZ800);
ArtronShop_BH1750 bh1750(0x23, &Wire); // Non Jump ADDR: 0x23, Jump ADDR: 0x5C
 
enum {NONE, RED, GREEN, BLUE};
int ledColor = NONE;

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

void setup()
{
  Serial.begin(9600);

  pixels.begin();

  analogReadResolution(ADC_BITS);
  analogWriteResolution(ANALOG_WRITE_RESOLUTION);

  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.setClock(I2C_SPEED_HZ);
  Wire.begin();

  bool error = false;  
  int bh1750Retries = 0;
  while (!bh1750.begin()) {
    error = true;
    bh1750Retries++;
    if(bh1750Retries == MAX_RETRIES) {
      break;
    }
    Serial.println("BH1750 not found !");
    delay(1000);
  }

  setLEDColor((error) ? RED : GREEN);

}
 
#define PWM_10V 180
#define PWM_16V 450

float light;
float voltage;
int pwmValueFinal;
int adcValuePot;

void loop()
{
  light = bh1750.light();

  int v = getAverageValueFrom(A0);
  
  float dividerVoltge = (v * ADC_VREF) / ((1 << ADC_BITS) - 1);
  float local_voltage = dividerVoltge * (R1 + R2) / R2;  
  voltage = roundfWithPrecisionTo(local_voltage, 1);

  adcValuePot = getAverageValueFrom(A1);

  int cor = mapfloat(voltage, 9.0, 16.0, 0.0, 100.0);
  int pwmValue = adcValuePot + cor;
  
  pwmValueFinal = map(pwmValue, 0, (1 << ADC_BITS), 0, (1 << ANALOG_WRITE_RESOLUTION));

  int max = 18000;
  if(light > max) {
    light = max;
  }
  //pwmValueFinal = mapfloat(light, max, 1000, pwmValueFinal, 30);

  analogWrite(PIN_LAMP, pwmValueFinal);
  delay(10);
}

void loop1() {
  Serial.print("Light: ");
  Serial.print(light);
  Serial.print(" lx");
  Serial.print(" voltage:");
  Serial.print(voltage);
  Serial.print(" ");
  Serial.print(pwmValueFinal);
  Serial.print(" ");
  Serial.print(adcValuePot);
  Serial.println();
  delay(5);
}