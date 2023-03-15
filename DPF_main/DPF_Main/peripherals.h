#ifndef PERIP_F_0
#define PERIP_F_0

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <canDefinitions.h>

#include "logic.h"
#include "can.h"
#include "tools.h"

#define ADC_BITS 12

//DPF heater
#define HEATER 14
//fuel valves
#define VALVES 13

//buttons
#define S_LEFT 11
#define S_RIGHT 12

//thermocouple
#define THERMOC A0

//pressure sensor
#define PRESSURE A1

//system power supply level (v)
#define VOLTS A2

//CAN interrupt GPIO number
#define CAN1_INT 15

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

#define MAX_LINES 7

#define M_LEFT 1
#define M_RIGHT 2
#define M_WHOLE 0

void displayInit(void);
void tx(int x, int y, const __FlashStringHelper *txt);
int getTxHeight(const __FlashStringHelper *txt);
int getTxWidth(const __FlashStringHelper *txt);
void quickDisplay(int line, int mode, const char *format, ...);
bool displayOptions(const char *left, const char *right);
int getDefaultTextHeight(void);
void show(void);
void hardwareInit(void);
bool readPeripherals(void *argument);
float adcToVolt(int adc, float r1, float r2);
void clearDisplay(void);
bool displayScreenFrom(const char *first, ...);
void clearLines(int from, int to, int mode);
void clearLine(int line, int mode);
void enableHeater(bool state);
void enableValves(bool state);

#endif
