#ifndef PERIP_F_0
#define PERIP_F_0

#include <Adafruit_NeoPixel.h>
#include <ArtronShop_BH1750.h>
#include "can.h"

enum {NONE, RED, GREEN, BLUE};

#define BH1750_ADDR 0x23 // Non Jump ADDR: 0x23, Jump ADDR: 0x5C

#define PIN_MISO 12
#define PIN_MOSI 11
#define PIN_SCK 10

#define CAN1_INT 8
#define CAN_CS 13

#define PIN_RGB 16
#define PIN_LAMP 2

#define NUMPIXELS 1

#define ADC_BITS 12
#define PIN_SDA 0
#define PIN_SCL 1
#define I2C_SPEED_HZ 50000

#define MAX_RETRIES 15

#define ANALOG_WRITE_RESOLUTION 9

#define MAX_VOLTAGE 16.0

#define ADC_VREF 3.3
#define R1 3300 // R up (3.3kΩ)
#define R2 470 // R down (470Ω)

bool setupPeripherials(void);
void setLEDColor(int ledColor);
float getLumens(void);
float getSystemVoltage(void);
int getValuePot(void);

#endif
