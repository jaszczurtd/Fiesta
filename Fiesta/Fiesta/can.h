#ifndef T_CAN
#define T_CAN

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <tools.h>

#include <canDefinitions.h>

#include "sensors.h"
#include "config.h"
#include "start.h"
#include "rpm.h"

extern float valueFields[];

#define CAN0_GPIO 17
#define CAN0_INT 15

#define CAN1_GPIO 6
#define CAN1_INT 14

bool canMainLoop(void *argument);
void canInit(int retries);
void sendThrottleValueCAN(int value);
bool updateCANrecipients(void *argument);
bool isDPFConnected(void);
bool canCheckConnection(void *message);

#endif

