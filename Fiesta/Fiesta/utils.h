#ifndef T_UTILS
#define T_UTILS

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Arduino.h>
#include <Wire.h>
#include <PCF8574.h>
#include "graphics.h"

static char debb[128];
#define deb(format, ...) { memset(debb, 0, sizeof(debb));  snprintf(debb, sizeof(debb) - 1, format, ## __VA_ARGS__); Serial.println(debb); }

#define PCF8574_ADDR 0x38

// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 21   
// how many samples to take and average, more takes longer
// but is more 'smooth'
#define NUMSAMPLES 6
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3600

#define TEMP_BAR_MAXHEIGHT 30

#define TEMP_OIL_MAX 155
#define TEMP_OIL_OK_HI 115

#define TEMP_MAX 120
#define TEMP_MIN 45

#define TEMP_OK_LO 70
#define TEMP_OK_HI 105

int binatoi(char *s);
char *decToBinary(int n);
unsigned char BinToBCD(unsigned char bin);
unsigned char reverse(unsigned char b);
void floatToDec(float val, int *hi, int *lo);
float adcToVolt(float basev, int adc);
float ntcToTemp(int tpin, int thermistor, int r);
void valToPWM(unsigned char pin, unsigned char val);
void drawImage(int x, int y, int width, int height, int background, unsigned int *pointer);
int percentToWidth(float percent, int maxWidth);
int textWidth(const char* text);
int textHeight(const char* text);
void drawTempValue(int x, int y, int valToDisplay);
int currentValToHeight(int currentVal, int maxVal);
void drawTempBar(int x, int y, int currentHeight, int color);
void displayErrorWithMessage(int x, int y, const char *msg);
void pcf857_init(void);
void pcf8574(unsigned char pin, bool value);
void i2cScanner(void);
void init4051(void);
void set4051ActivePin(unsigned char pin);
float getAverageValueFrom(int tpin);

#endif
