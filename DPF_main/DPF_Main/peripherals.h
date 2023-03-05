#ifndef PERIP_F_0
#define PERIP_F_0

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>

#include "logic.h"
#include "can.h"

#include "../../../canDefinitions.h"

//DPF heater
#define HEATER 14

#define VALVES 13

//CAN interrupt GPIO number
#define CAN1_INT 15

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

void displayInit(void);
void tx(int x, int y, const __FlashStringHelper *txt);
int getTxHeight(const __FlashStringHelper *txt);
int getTxWidth(const __FlashStringHelper *txt);
void quickDisplay(int val1, int val2);
int getDefaultTextHeight(void);
void show(void);
void hardwareInit(void);

#endif
