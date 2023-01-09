#ifndef CAN_F_0
#define CAN_F_0

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <arduino-timer.h>

#include "../../../canDefinitions.h"

#define deb(format, ...) { \
    char buffer[100]; \
    memset (buffer, 0, sizeof(buffer)); \
    snprintf(buffer, sizeof(buffer) - 1, format, ## __VA_ARGS__); \
    Serial.println(buffer); \
    }

//GPIO number
#define CAN1_INT 13

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)


void initializations(void);
void looper(void);

#endif

