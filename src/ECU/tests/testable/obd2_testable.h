#ifndef ECU_TESTABLE_OBD2_H
#define ECU_TESTABLE_OBD2_H

#include "dtcManager.h"
#include "hal/can/hal_can.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef UNIT_TEST
#ifdef __cplusplus
extern "C" {
#endif

uint8_t stMinToMs(uint8_t stMin);
uint8_t obd_encodeTempByte(float tempC);
hal_can_t obdTestGetCanHandle(void);
void obdTestResetTransport(void);
void obdReqWithDlc(uint32_t requestId, uint8_t dlc, const uint8_t *data);
#define obdReq(requestId, data) obdReqWithDlc((requestId), 8u, (data))
bool encodeMode01PidData(uint8_t pid, uint8_t *out, int *outLen);
int fillDtcPayload(uint8_t responseService, dtc_kind_t kind, uint8_t *outData,
                   int maxLen);
bool fordPartNumberSplit(const char *pn, const char **prefixOut, int *prefixLen,
                         const char **middleOut, int *middleLen,
                         const char **suffixOut, int *suffixLen);
uint8_t fordPartSuffixCharsToByte(const char *s, int len);

#ifdef __cplusplus
}
#endif
#endif

#endif
