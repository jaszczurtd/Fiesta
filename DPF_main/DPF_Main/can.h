#ifndef CAN_F_0
#define CAN_F_0

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <arduino-timer.h>

#include "peripherals.h"
#include "logic.h"

void canInit(void);
void canMainLoop(void);
void receivedCanMessage(void);
bool callAtSomeTime(void *argument);


#endif

