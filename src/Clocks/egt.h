#ifndef C_EGT
#define C_EGT

#include "clocksState.h"
#include "config.h"

#include <stdint.h>

// Both EGT thermocouples on MCP9600 converters (I2C0), core 1. A converter
// that is missing or stops answering leaves its channel without a
// measurement and is looked for again every THERMOCOUPLE_RETRY_INTERVAL;
// the other channel keeps working. No call waits for a converter.

/** @brief Look for both converters once; does not wait for missing ones. */
void egtInit(void);

/**
 * @brief Read both channels; call every THERMOCOUPLE_READ_INTERVAL.
 * @param local Receives egtC, egtValid and egtUpdatedMs of both channels.
 * @param nowMs hal_millis() of this pass.
 */
void egtRead(clocks_local_snapshot_t *local, uint32_t nowMs);

#endif
