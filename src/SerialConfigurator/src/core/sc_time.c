/**
 * @file sc_time.c
 * @brief Monotonic clock and sleep shared by the host polling loops.
 */

#include "sc_time.h"

#include <time.h>

uint64_t sc_time_monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0u;
  }
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

void sc_time_sleep_ms(uint32_t ms) {
  if (ms == 0u) {
    return;
  }
  struct timespec req;
  req.tv_sec = (time_t)(ms / 1000u);
  req.tv_nsec = (long)((ms % 1000u) * 1000000L);
  (void)nanosleep(&req, NULL);
}
