#ifndef CAN_F_0
#define CAN_F_0

#include "../common/canDefinitions/canDefinitions.h"
#include "hardwareConfig.h"
#include <JaszczurHAL.h>

#include "start.h"

bool canInit(void);
void canMainLoop(void);
void updateCANrecipients(void);
void updateEGTrecipients(void);
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
/** @brief Create and start the periodic broadcast timers: the oil/speed frame
 * every CAN_UPDATE_RECIPIENTS, the EGT frame every CAN_EGT_UPDATE_INTERVAL.
 * @return False when the timer table could not be set up. */
bool canSetupBroadcastTimers(void);
/** @brief Tick the broadcast timers; call from the core-0 loop. */
void canTickBroadcastTimers(void);

#endif
