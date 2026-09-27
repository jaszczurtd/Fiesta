#ifndef SC_TIME_H
#define SC_TIME_H

/**
 * @file sc_time.h
 * @brief Monotonic clock and sleep shared by the host polling loops
 *        (flash watcher, test status, BUSY retries).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Milliseconds of CLOCK_MONOTONIC; 0 when the clock fails. */
uint64_t sc_time_monotonic_ms(void);

/** @brief Sleep for @p ms; a signal may end it early. */
void sc_time_sleep_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* SC_TIME_H */
