#ifndef FIESTA_CLOCK_TIME_H_
#define FIESTA_CLOCK_TIME_H_

#include "RTC.h"

#include <JaszczurHAL.h>

/* The RTC keeps UTC; the display, the buttons and SC use CET/CEST. A KV
 * marker records that this firmware set the RTC to UTC. Until then the RTC
 * holds the local time of the older firmware: it is shown as is and is not
 * reported as trusted UTC.
 *
 * A UTC write stores the marker once the RTC has been read back. While the
 * flash answers that the marker is missing, a failed read-back or marker
 * write puts the same instant back as local time. When the flash does not
 * answer, the marker may be there: the RTC keeps UTC, shown as such but not
 * trusted, until clockTimeSettle() confirms the marker. A flash that cannot
 * be read at start shows the RTC as UTC too, without trust. */

/**
 * @brief Open the KV store and read the UTC marker; call after PCF_Init().
 * @return HAL_OK, or the storage error; without the marker every time write
 *         tries to store it again.
 */
hal_status_t clockTimeInit(void);

/** @return true while the flash holds the UTC marker; only then is the time
 *          trusted. */
bool clockTimeIsUtc(void);

/** @return false while the RTC holds UTC read back after a write, but the
 *          marker is not confirmed in the flash. */
bool clockTimeSettled(void);

/**
 * @brief Finish an unsettled change to UTC: store the marker, or go back to
 *        local time when the flash answers without it.
 * @return HAL_OK when settled, the RTC read error, or the KV error.
 */
hal_status_t clockTimeSettle(void);

/**
 * @brief Check that UTC seconds fall in CFG_RTC_MIN_YEAR..CFG_RTC_MAX_YEAR.
 * @param epoch UTC seconds.
 * @return true for a time the clock may keep.
 */
bool clockTimeEpochInRange(uint32_t epoch);

/**
 * @brief Read the RTC as UTC seconds.
 * @param[out] outEpoch UTC seconds, or 0 when not trusted.
 * @param[out] outTrusted true for a valid date in range, with RTC integrity
 *             and the UTC marker.
 * @return HAL_OK when the RTC answered, also with contents that are no valid
 *         date; HAL_EINVAL for NULL outputs, or the read error, e.g. HAL_EIO
 *         for a bus error.
 */
hal_status_t clockTimeReadUtc(uint32_t *outEpoch, bool *outTrusted);

/**
 * @brief Read the time to show and edit.
 *
 * CET/CEST converted from UTC, or the RTC fields as they are before the UTC
 * marker exists. The weekday follows the date: 0 = Monday ... 6 = Sunday.
 *
 * @param[out] outLocal Local date and time; unchanged on error.
 * @param[out] outTrusted Optional; true when converted from trusted UTC.
 * @return HAL_OK, HAL_EINVAL for a NULL @p outLocal, HAL_EPROTO when the RTC
 *         holds no valid date, or the read error, e.g. HAL_EIO for a bus error.
 */
hal_status_t clockTimeReadLocal(PCF_DateTime *outLocal, bool *outTrusted);

/**
 * @brief Write UTC seconds, check them by reading back, then store the marker.
 *
 * The display follows the write the RTC took, also without the read-back.
 * Until the marker is stored the time is not trusted; see the file comment
 * for a marker that the flash refused or did not confirm.
 *
 * @param epoch UTC seconds.
 * @return HAL_OK, HAL_EINVAL outside clockTimeEpochInRange(), HAL_EIO when
 *         the RTC refused the write or read back another time or no
 *         integrity, or the KV error.
 */
hal_status_t clockTimeWriteUtc(uint32_t epoch);

/**
 * @brief Write a CET/CEST date and time as UTC.
 *
 * The weekday field is ignored. A local time skipped by the spring change is
 * taken as CET and shows one hour later as CEST; the repeated autumn hour is
 * taken as its first, CEST pass.
 *
 * @param local Local date and time.
 * @return As clockTimeWriteUtc(); HAL_EINVAL also for an invalid date.
 */
hal_status_t clockTimeWriteLocal(const PCF_DateTime *local);

/**
 * @brief Set the weekday (0 = Monday) from the date.
 * @param dt Date to update; an invalid date keeps its weekday.
 */
void clockTimeUpdateWeekday(PCF_DateTime *dt);

#endif /* FIESTA_CLOCK_TIME_H_ */
