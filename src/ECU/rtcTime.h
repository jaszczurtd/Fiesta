#ifndef T_RTC_TIME
#define T_RTC_TIME

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Store the time of a 0x130 frame of Fiesta_clock received just now.
 *
 * Core 0 only: the CAN receive path is the single writer.
 * @param utc UTC seconds of a frame with integrity 1, or 0 for a frame
 *            without trusted time.
 */
void rtcTimeStore(uint32_t utc);

/**
 * @brief Forget a time received RTC_TIME_MAX_AGE_MS or more ago.
 *
 * Core 0 only, called periodically, so that a long silence never looks
 * fresh again once hal_millis() wraps.
 */
void rtcTimeExpire(void);

/**
 * @brief Current UTC from the last trusted 0x130 frame.
 *
 * Either core may call it; it never waits for the writer. The frame time is
 * advanced by the whole seconds since its reception.
 * @return UTC seconds; 0 before the first trusted frame, when the last frame
 *         had no trusted time, when it arrived RTC_TIME_MAX_AGE_MS or more
 *         ago, or when the writer changed the time during every read attempt.
 */
uint32_t rtcTimeGetUtc(void);

#ifdef UNIT_TEST
/** @brief Called between the two time words of each read attempt. */
extern void (*rtcTimeReadHookForTest)(void);
/** @brief Forget the stored time and remove the read hook. */
void rtcTimeResetForTest(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
