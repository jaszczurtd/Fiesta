 
#include <Adafruit_NeoPixel.h>
#include <ArtronShop_BH1750.h>
#include <Wire.h>

#define PIN_RGB 16
#define NUMPIXELS 1

#define ADC_BITS 12
#define PIN_SDA 0
#define PIN_SCL 1
#define I2C_SPEED_HZ 50000

#define PIN_LAMP 2

#define ANALOG_WRITE_RESOLUTION 9

#define R1 3300 // Rezystor górny (3.3kΩ)
#define R2 470 // Rezystor dolny (470Ω)

Adafruit_NeoPixel pixels(NUMPIXELS, PIN_RGB, NEO_GRB + NEO_KHZ800);
ArtronShop_BH1750 bh1750(0x23, &Wire); // Non Jump ADDR: 0x23, Jump ADDR: 0x5C
 
enum {NONE, RED, GREEN, BLUE};
int ledColor = NONE;

bool state = false;

void setup()
{
  Serial.begin(9600);

  pixels.begin();

  analogReadResolution(ADC_BITS);
  analogWriteResolution(ANALOG_WRITE_RESOLUTION);
  analogWrite(PIN_LAMP, 508);

  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.setClock(I2C_SPEED_HZ);
  Wire.begin();
  
  while (!bh1750.begin()) {
    Serial.println("BH1750 not found !");
    delay(1000);
  }

}
 
void loop()
{
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
 
  ledColor++;
  if (ledColor == 4) {
    ledColor = NONE;
  }

  int v = analogRead(A0);
  float napiecieDzielnika = (v * 3.3) / 4095.0;
  float napiecieZasilacza = napiecieDzielnika * (R1 + R2) / R2;  

  Serial.print("Light: ");
  Serial.print(bh1750.light());
  Serial.print(" lx");
  Serial.print(" voltage:");
  Serial.print(napiecieZasilacza);

  Serial.println();



  delay(200);

}