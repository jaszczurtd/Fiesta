#ifndef CAN_F_0
#define CAN_F_0

#include <tools.h>
#include <canDefinitions.h>
#include "hardwareConfig.h"

#include "start.h"

bool canInit(void);
void canMainLoop(void);
void updateCANrecipients(void);
void canCheckConnection(void);
bool isEcuConnected(void);
bool isClusterConnected(void);
bool isDPFConnected(void);
bool isFanEnabled(void);
bool isDPFRegenerating(void);
float readFuel(void);
bool isGPSAvailable(void);
bool isEngineRunning(void);
int getEngineRPM(void);
bool canSendLoop(void);

#endif
