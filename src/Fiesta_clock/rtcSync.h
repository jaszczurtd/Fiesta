#ifndef FIESTA_CLOCK_RTC_SYNC_H_
#define FIESTA_CLOCK_RTC_SYNC_H_

#include <stdbool.h>
#include <stdint.h>

/* Sets the RTC from the GPS UTC of Clocks (CAN_ID_GPS_UTC_UPDATE). Each pass
 * of the main loop reads the receive queue into rtcSyncReceive() and then
 * calls rtcSyncTick(). Without frames the RTC keeps running on its own. */

/**
 * @brief Start with no frame and no pending limits.
 * @param nowMs hal_millis() at the start.
 */
void rtcSyncInit(uint32_t nowMs);

/**
 * @brief Take one received GPS UTC frame; the newest of a pass counts.
 * @param data Frame payload.
 * @param len Payload length; a frame without the status byte is ignored.
 */
void rtcSyncReceive(const uint8_t *data, uint8_t len);

/**
 * @brief Use the frame of this pass and run the correction limits.
 *
 * The frame sets an RTC without trusted UTC at once. A trusted RTC is
 * corrected when it is more than CFG_RTC_SYNC_TOLERANCE_S off and no
 * correction happened within CFG_RTC_SYNC_CORRECTION_INTERVAL_MS. A failed
 * RTC or KV access stops use of frames for CFG_RTC_SYNC_RETRY_MS; with the
 * same period an unsettled change to UTC is finished by clockTimeSettle(),
 * with or without frames. Frames read
 * more than CFG_RTC_SYNC_MAX_RX_GAP_MS after the previous pass, and frames
 * read while the time is being edited, are dropped.
 *
 * @param nowMs hal_millis() of this pass.
 * @param timeEditActive true while the buttons or SC edit the time.
 */
void rtcSyncTick(uint32_t nowMs, bool timeEditActive);

#endif /* FIESTA_CLOCK_RTC_SYNC_H_ */
