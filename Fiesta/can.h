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
#include "tests.h"

extern volatile float valueFields[];

bool canMainLoop(void *argument);
void canInit(int retries);
void CAN_sendUpdate(void);
void sendThrottleValueCAN(int value);
bool updateCANrecipients(void *argument);
bool isDPFConnected(void);
bool canCheckConnection(void *message);

#endif

