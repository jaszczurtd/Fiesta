#ifndef FIESTA_CLOCK_CAN_H_
#define FIESTA_CLOCK_CAN_H_

#include <JaszczurHAL.h>

/**
 * @brief Create the CAN channel for the RTC and air temperature broadcasts.
 * @return HAL_OK, or the error of the last creation attempt; the clock then
 *         keeps running without CAN.
 */
hal_status_t clockCanInit(void);
/**
 * @brief Read the GPS UTC frames, set the RTC from them and send the RTC time
 *        once a second. The air temperatures go out with it and at once when
 *        a reading or its validity changes; call ds18b20_poll() before.
 *        Without CAN it still finishes an unfinished change of the RTC to
 *        UTC.
 * @param timeEditActive true while the buttons or SC edit the time; GPS then
 *        leaves the RTC alone.
 */
void clockCanTick(bool timeEditActive);

#endif /* FIESTA_CLOCK_CAN_H_ */
