#ifndef C_LAMP_BLINK
#define C_LAMP_BLINK

#include <stdint.h>

/**
 * @brief Blink phase of a cluster lamp.
 * @param nowMs Current hal_millis().
 * @param sinceMs hal_millis() when the blinking started.
 * @param halfPeriodMs Lit time, and the dark time after it.
 * @return True for the lit half of every period counted from @p sinceMs.
 */
static inline bool lampBlinkLit(uint32_t nowMs, uint32_t sinceMs,
                                uint32_t halfPeriodMs) {
  return (((uint32_t)(nowMs - sinceMs) / halfPeriodMs) % 2u) == 0u;
}

#endif
