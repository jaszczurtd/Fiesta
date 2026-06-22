#include "can.h"

#include "RTC.h"
#include "hardwareConfig.h"

#include "../common/canDefinitions/canDefinitions.h"

#include <tools_c.h>

#define CLOCK_CAN_RETRIES 4
#define CLOCK_CAN_SEND_INTERVAL_MS 1000u

typedef struct {
  hal_can_t handle;
  bool initialized;
  uint8_t frameNumber;
  uint32_t nextSendMs;
} clock_can_state_t;

static clock_can_state_t s_clockCanState = {
    .handle = NULL, .initialized = false, .frameNumber = 0u, .nextSendMs = 0u};

static bool rtcDatetimeIsValid(const PCF_DateTime *dt) {
  if (dt == NULL) {
    return false;
  }

  if (dt->minute > 59u || dt->hour > 23u || dt->day < 1u || dt->day > 31u ||
      dt->month < 1u || dt->month > 12u) {
    return false;
  }

  return true;
}

static uint32_t packRtcDateTime(const PCF_DateTime *dt) {
  if (!rtcDatetimeIsValid(dt)) {
    return 0u;
  }

  int32_t yearOffset = dt->year - 2020;
  if (yearOffset < 0) {
    yearOffset = 0;
  } else if (yearOffset > 15) {
    yearOffset = 15;
  }

  return ((uint32_t)yearOffset << 20) | ((uint32_t)dt->month << 16) |
         ((uint32_t)dt->day << 11) | ((uint32_t)dt->hour << 6) |
         (uint32_t)dt->minute;
}

void clockCanInit(void) {
  s_clockCanState.frameNumber = 0u;
  s_clockCanState.nextSendMs = hal_millis() + CLOCK_CAN_SEND_INTERVAL_MS;
  hal_can_config_t canCfg = hal_can_default_config();
  canCfg.mcp2515.cs_pin = CAN_CS;

  s_clockCanState.handle = hal_can_create_with_retry(
      &canCfg, CAN_INT, NULL, CLOCK_CAN_RETRIES - 1, NULL);
  s_clockCanState.initialized = (s_clockCanState.handle != NULL);
}

void clockCanTick(void) {
  if (!s_clockCanState.initialized) {
    return;
  }

  const uint32_t nowMs = hal_millis();
  if ((int32_t)(nowMs - s_clockCanState.nextSendMs) < 0) {
    return;
  }
  s_clockCanState.nextSendMs = nowMs + CLOCK_CAN_SEND_INTERVAL_MS;

  PCF_DateTime dt = {0};
  const unsigned char dtResult = PCF_GetDateTime(&dt);
  bool integrityOk = (dtResult == 0u);
  if (PCF_GetClockIntegrity(&integrityOk) != 0u) {
    integrityOk = (dtResult == 0u);
  }
  if (!integrityOk || !rtcDatetimeIsValid(&dt)) {
    return;
  }

  uint8_t out[CAN_FRAME_MAX_LENGTH] = {0};
  const uint32_t packedDateTime = packRtcDateTime(&dt);

  out[CAN_FRAME_NUMBER] = s_clockCanState.frameNumber++;
  out[CAN_FRAME_RTC_UPDATE_DT_HI] = (uint8_t)((packedDateTime >> 16) & 0xFFu);
  out[CAN_FRAME_RTC_UPDATE_DT_MD] = (uint8_t)((packedDateTime >> 8) & 0xFFu);
  out[CAN_FRAME_RTC_UPDATE_DT_LO] = (uint8_t)(packedDateTime & 0xFFu);
  out[CAN_FRAME_RTC_UPDATE_SECOND] = dt.second;
  out[CAN_FRAME_RTC_UPDATE_INTEGRITY] = integrityOk ? 1u : 0u;

  hal_can_send(s_clockCanState.handle, CAN_ID_RTC_UPDATE, CAN_FRAME_MAX_LENGTH,
               out);
}
