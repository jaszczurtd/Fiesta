#ifndef CAN_F_0
#define CAN_F_0

#include "../common/canDefinitions/canDefinitions.h"
#include "logic.h"
#include <JaszczurHAL.h>
#include <hal/hal.h>

#include "clocksState.h"

// Core 0: receives and sends the Clocks frames. The display reads the ECU
// state through the snapshot of clocksState.h.

/**
 * @brief Create the CAN channel and set the Fiesta acceptance filters.
 * @return HAL_OK, or the error of the channel creation or of the first filter
 *         that failed (the channel is released then).
 */
hal_status_t canInit(void);
/** @brief Drain received frames and publish the changed ECU state. */
void canMainLoop(void);
void receivedCanMessage(void);
/** @brief Send the brightness and the oil pressure and ABS speed frames. */
void updateCANrecipients(void);
/** @brief Send the EGT frame of both channels with their validity. */
void updateEGTrecipients(void);
/** @brief Send UTC every second; legacy GPS IDs default to disabled. */
void updateGPSrecipients(void);
/** @brief Create the timers of the periodic frames (core 0).
 * @return False when the timer table could not be set up. */
bool canSetupBroadcastTimers(void);
/** @brief Tick the periodic frame timers; call from the core-0 loop. */
void canTickBroadcastTimers(void);
/** @brief Refresh the connection states from the frame counters. */
void canCheckConnection(void);
/** @return True when the MCP2515 signalled a frame not drained yet. */
bool canRxPending(void);
/**
 * @brief ECU value as decoded on core 0.
 * @param idx F_* index; out of range returns 0.
 */
float canGetValue(int idx);
/**
 * @brief Whether a value was decoded or measured on core 0 lately.
 * @param idx F_* index; out of range is never fresh.
 * @param nowMs Current hal_millis().
 * @param maxAgeMs Age limit; a value exactly that old is no longer fresh.
 * @return False also for a value never decoded since canInit(). Clearing the
 *         ECU values after its loss does not refresh them.
 */
bool canValueFresh(int idx, uint32_t nowMs, uint32_t maxAgeMs);
/**
 * @brief Set a value measured on core 0 (the ABS speed); published with the
 *        next CAN drain.
 * @param idx F_* index; out of range is ignored.
 * @param val New value.
 */
void canSetValue(int idx, float val);

#endif
