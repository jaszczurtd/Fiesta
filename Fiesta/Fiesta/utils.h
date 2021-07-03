#ifndef T_UTILS
#define T_UTILS

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Arduino.h>
#include <Wire.h>

#include "graphics.h"
#include "config.h"

#define PCF8574_ADDR 0x38

#define TEMP_BAR_MAXHEIGHT 29

void floatToDec(float val, int *hi, int *lo);
float adcToVolt(float basev, int adc);
float ntcToTemp(int tpin, int thermistor, int r);
void valToPWM(unsigned char pin, int val);
int percentToWidth(float percent, int maxWidth);
int currentValToHeight(int currentVal, int maxVal);
void pcf857_init(void);
void pcf8574_write(unsigned char pin, bool value);
#ifdef I2C_SCANNER
void i2cScanner(void);
#endif
void init4051(void);
void set4051ActivePin(unsigned char pin);
float getAverageValueFrom(int tpin);
unsigned long getSeconds(void);

#endif
