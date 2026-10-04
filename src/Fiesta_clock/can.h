#ifndef FIESTA_CLOCK_CAN_H_
#define FIESTA_CLOCK_CAN_H_

#include <JaszczurHAL.h>

/**
 * @brief Create the CAN channel for the RTC broadcast.
 * @return HAL_OK, or the error of the last creation attempt; the clock then
 *         keeps running without CAN.
 */
hal_status_t clockCanInit(void);
void clockCanTick(void);

#endif /* FIESTA_CLOCK_CAN_H_ */
