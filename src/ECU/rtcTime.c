#include "rtcTime.h"

#include "config.h"
#include <hal/core/hal_compiler.h>
#include <hal/system/hal_system.h>

#include <stdbool.h>
#include <stddef.h>

/* A reader gives up after this many attempts that each overlapped a store,
 * so neither core ever waits for the other. */
#define RTC_TIME_READ_ATTEMPTS 4u

/* Odd while rtcTimeStore() changes the two words below. */
static uint32_t s_sequence = 0u;
static uint32_t s_utc = 0u; /* 0 = no trusted time */
static uint32_t s_receivedMs = 0u;

#ifdef UNIT_TEST
void (*rtcTimeReadHookForTest)(void) = NULL;
#endif

static void publish(uint32_t utc, uint32_t receivedMs) {
  const uint32_t sequence = HAL_ATOMIC_LOAD(&s_sequence, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&s_sequence, sequence + 1u, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_THREAD_FENCE(HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&s_utc, utc, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&s_receivedMs, receivedMs, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&s_sequence, sequence + 2u, HAL_ATOMIC_RELEASE);
}

void rtcTimeStore(uint32_t utc) { publish(utc, hal_millis()); }

void rtcTimeExpire(void) {
  /* Core 0 is the only writer, so its own words need no sequence check. */
  if ((HAL_ATOMIC_LOAD(&s_utc, HAL_ATOMIC_RELAXED) != 0u) &&
      hal_millis_deadline_expired(
          HAL_ATOMIC_LOAD(&s_receivedMs, HAL_ATOMIC_RELAXED),
          RTC_TIME_MAX_AGE_MS)) {
    publish(0u, hal_millis());
  }
}

uint32_t rtcTimeGetUtc(void) {
  uint32_t result = 0u;
  bool consistent = false;
  for (uint32_t attempt = 0u; (attempt < RTC_TIME_READ_ATTEMPTS) && !consistent;
       attempt++) {
    const uint32_t sequence = HAL_ATOMIC_LOAD(&s_sequence, HAL_ATOMIC_ACQUIRE);
    const uint32_t utc = HAL_ATOMIC_LOAD(&s_utc, HAL_ATOMIC_RELAXED);
#ifdef UNIT_TEST
    if (rtcTimeReadHookForTest != NULL) {
      rtcTimeReadHookForTest();
    }
#endif
    const uint32_t receivedMs =
        HAL_ATOMIC_LOAD(&s_receivedMs, HAL_ATOMIC_RELAXED);
    HAL_ATOMIC_THREAD_FENCE(HAL_ATOMIC_ACQUIRE);
    consistent = ((sequence & 1u) == 0u) &&
                 (HAL_ATOMIC_LOAD(&s_sequence, HAL_ATOMIC_RELAXED) == sequence);
    if (consistent) {
      const uint32_t nowMs = hal_millis();
      if ((utc != 0u) &&
          !hal_elapsed_u32(nowMs, receivedMs, RTC_TIME_MAX_AGE_MS)) {
        result = utc + ((nowMs - receivedMs) / 1000u);
      }
    }
  }
  return result;
}

#ifdef UNIT_TEST
void rtcTimeResetForTest(void) {
  rtcTimeReadHookForTest = NULL;
  publish(0u, 0u);
}
#endif
