#ifndef CAN_F_0
#define CAN_F_0

#include <Wire.h>
#include <SPI.h>
//coryjfowler
#include <mcp_can.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
//Michael Contreras
#include <arduino-timer.h>

#include <canDefinitions.h>

#include "peripherals.h"
#include "logic.h"
#include "tools.h"

extern float valueFields[];

void canInit(void);
bool canMainLoop(void *message);
void receivedCanMessage(void);
bool callAtHalfSecond(void *argument);
bool canCheckConnection(void *message);
bool isEcuConnected(void);

#endif

