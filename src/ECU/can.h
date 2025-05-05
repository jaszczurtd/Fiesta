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
void CAN_sendAll(void);
void CAN_sendThrottleUpdate(void);
void CAN_sendTurboUpdate(void);
bool CAN_updaterecipients_01(void *argument);
void CAN_updaterecipients_02(void);
void sendThrottleValueCAN(int value);
bool isDPFConnected(void);
bool canCheckConnection(void *message);

#endif

