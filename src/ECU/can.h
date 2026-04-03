#ifndef T_CAN
#define T_CAN

#include <tools.h>
#include <canDefinitions.h>

#include "sensors.h"
#include "config.h"
#include "rpm.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

void canMainLoop(void);
void canInit(int retries);
void CAN_sendAll(void);
void CAN_sendThrottleUpdate(void);
void CAN_sendTurboUpdate(void);
void CAN_updaterecipients_01(void);
void CAN_updaterecipients_02(void);
void sendThrottleValueCAN(int value);
uint32_t CAN_packGpsDateTime(uint32_t dateYYMMDD, uint32_t timeHHMM);
bool CAN_buildGpsLatFrame(uint8_t frameNo, uint8_t *outBuf, int outLen);
bool CAN_buildGpsLonTimeFrame(uint8_t frameNo, uint8_t *outBuf, int outLen);
void CAN_sendGpsExtended(void);
bool isDPFConnected(void);
void canCheckConnection(void);

#ifdef __cplusplus
}
#endif

#endif
