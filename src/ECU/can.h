#ifndef T_CAN
#define T_CAN

#include <tools.h>
#include <canDefinitions.h>

#include "sensors.h"
#include "config.h"
#include "start.h"
#include "rpm.h"
#include "tests.h"

void canMainLoop(void);
void canInit(int retries);
void CAN_sendAll(void);
void CAN_sendThrottleUpdate(void);
void CAN_sendTurboUpdate(void);
void CAN_updaterecipients_01(void);
void CAN_updaterecipients_02(void);
void sendThrottleValueCAN(int value);
bool isDPFConnected(void);
void canCheckConnection(void);

#endif

