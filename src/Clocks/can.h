#ifndef CAN_F_0
#define CAN_F_0

#include "../common/canDefinitions/canDefinitions.h"
#include "logic.h"
#include <JaszczurHAL.h>
#include <hal/hal.h>

extern volatile float valueFields[];

bool canInit(void);
void canMainLoop(void);
void receivedCanMessage(void);
void updateCANrecipients(void);
void canCheckConnection(void);
bool isEcuConnected(void);
bool isDPFConnected(void);
bool isOilSpeedModuleConnected(void);
bool isFanEnabled(void);
bool isDPFRegenerating(void);
float readFuel(void);
int getCurrentCarSpeed(void);
int getGPSSpeed(void);
bool isGPSAvailable(void);
bool isEngineRunning(void);
int getEngineRPM(void);
float getOilPressure(void);

#endif
