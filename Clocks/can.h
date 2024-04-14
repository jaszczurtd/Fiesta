#ifndef CAN_F_0
#define CAN_F_0

#include <SPI.h>
//coryjfowler
#include <mcp_can.h>

#include <tools.h>
#include <canDefinitions.h>
#include <arduino-timer.h>

#include "logic.h"

extern float valueFields[];

bool canInit(void);
bool canMainLoop(void *message);
void receivedCanMessage(void);
bool updateCANrecipients(void *argument);
bool canCheckConnection(void *message);
bool isEcuConnected(void);
bool isDPFConnected(void);
bool isFanEnabled(void);
bool isDPFRegenerating(void);
float readFuel(void);
int getCurrentCarSpeed(void);
bool isGPSAvailable(void);
bool isEngineRunning(void);

#endif

