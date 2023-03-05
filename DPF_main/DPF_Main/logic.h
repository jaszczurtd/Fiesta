#ifndef LOGIC_F_0
#define LOGIC_F_0

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <hardware/watchdog.h>

#include "can.h"
#include "peripherals.h"

#include "../../../canDefinitions.h"

#define deb(format, ...) { \
    char buffer[100]; \
    memset (buffer, 0, sizeof(buffer)); \
    snprintf(buffer, sizeof(buffer) - 1, format, ## __VA_ARGS__); \
    Serial.println(buffer); \
    }

void initialization(void);
void looper(void);

#endif
