#ifndef T_CAN
#define T_CAN

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>

#include "sensors.h"
#include "config.h"
#include "start.h"
#include "utils.h"
#include "rpm.h"

//until I figure out how to deal better wit this with Arduino IDE...
#include "c:\development\projects_git\fiesta\canDefinitions.h"

#define CAN0_GPIO 17
#define CAN0_INT 15

void canMainLoop(void);
void canInit(void);
void sendThrottleValueCAN(int value);

#endif

