#ifndef CAN_F_0
#define CAN_F_0

#include <SPI.h>
//coryjfowler
#include <mcp_can.h>

#include <tools.h>
#include <canDefinitions.h>

#include "lights.h"

extern float valueFields[];

bool canInit(void);
bool canMainLoop(void *message);
void receivedCanMessage(void);
bool callAtHalfSecond(void *argument);
bool canCheckConnection(void *message);
bool isEcuConnected(void);

#endif

