#ifndef T_CAN
#define T_CAN

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>

#include <canDefinitions.h>

#include "sensors.h"
#include "config.h"
#include "start.h"
#include "utils.h"
#include "rpm.h"

extern float valueFields[];

#define CAN0_GPIO 17
#define CAN0_INT 15

bool canMainLoop(void *argument);
void canInit(void);
void sendThrottleValueCAN(int value);
bool updateCANrecipients(void *argument);
bool isDPFConnected(void);
bool canCheckConnection(void *message);

#endif

