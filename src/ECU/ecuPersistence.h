#ifndef ECU_PERSISTENCE_H
#define ECU_PERSISTENCE_H

#include <hal/core/hal_status.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef hal_status_t (*ecu_persistence_operation_fn)(const void *user);

/** @brief Initialize the shared persistence serializer on core 0. */
hal_status_t ecuPersistenceInit(void);

/**
 * @brief Serialize one core-0 storage operation.
 *
 * GPS is paused only by the EEPROM flash-write callbacks, immediately around
 * a physical write. Reads, validation and RAM staging leave GPS running. All
 * ECU EEPROM/KV writes and GPS polling must remain on core 0. The return value
 * reports the storage operation (including a failed write preparation).
 * A failed GPS resume is retained for retry by ecuPersistencePoll(). Optional
 * outResumeStatus receives the latest resume result, or HAL_NONE when no
 * physical write was attempted.
 */
hal_status_t ecuPersistenceExecute(ecu_persistence_operation_fn operation,
                                   const void *user,
                                   hal_status_t *outResumeStatus);

/**
 * @brief Erase the spare key-value bank ahead of the next compaction.
 *
 * Key-value writes only program the active bank; erasing stops both cores
 * for tens of milliseconds per flash sector. Call this only where such a pause
 * is harmless: at start-up, before core 1 runs the pump. Serialized like
 * every other storage operation, GPS paused around the erase.
 * @return HAL_OK when the spare bank is erased (at once when it already was),
 * HAL_EUNINIT before the key-value store is initialized, or the failure of
 * the GPS pause or of the erase.
 */
hal_status_t ecuPersistencePrepareStorage(void);

/**
 * @brief Retry a GPS resume left pending by a completed storage operation.
 *
 * Call from the core-0 service loop. Attempts are rate-limited internally.
 */
void ecuPersistencePoll(void);

#ifdef __cplusplus
}
#endif

#endif
